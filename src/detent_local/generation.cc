#include "paged_limits.hh"
#include "detent_local/generation.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/ownership.hh"
#include "detent_local/store_table.hh"
#include "names.hh"
#include "paged_root_prefix.hh"
#include "generation_mapping.hh"
#include "generation_file_access.hh"
#include "leased_paged_root.hh"
#include "paged_dependency_discovery.hh"
#include "paged_progress_view.hh"
#include "paged_warm_validator.hh"
#include "recovery_context.hh"
#include "resources.hh"
#include "segment_names.hh"
#include "trusted_restart.hh"
#include "warm_recovery_eligibility.hh"
#include "warm_registry_merge.hh"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sha256.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace detent::local {
namespace {
struct RecoverySelectionChanged : std::runtime_error {
	RecoverySelectionChanged() : std::runtime_error("managed recovery selection changed") {}
};
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("invalid local generation descriptor");
}
using File = detail::RecoveryFile;
using Directory = detail::GenerationDirectory;

using Mapping = detail::GenerationMapping;

std::size_t mapping_capacity(std::uint64_t length) {
	const auto page = ::sysconf(_SC_PAGESIZE);
	require(page > 0);
	const auto bytes = static_cast<std::size_t>(page);
	require(length <= std::numeric_limits<std::size_t>::max() - (bytes - 1));
	return ((static_cast<std::size_t>(length) + bytes - 1) / bytes) * bytes;
}
void trusted_file(int fd, uid_t owner, std::uint64_t length) {
	detail::check_generation_file(fd, owner, length);
}
void read_record(int fd, std::span<char> bytes) {
	detail::read_generation_record(fd, bytes);
}

} // namespace
void validate_descriptor(const Descriptor &d) {
	require(std::any_of(d.generation.begin(), d.generation.end(), [](char c) { return c != 0; }) &&
			d.length >= header_bytes);
	// Reuse the header codec to enforce identity and semantic profile invariants.
	Header h;
	h.cursor = d.cursor;
	h.profile = d.profile;
	encode_header(h);
}
bool same_generation(const Descriptor &a, const Descriptor &b) noexcept {
	return a.generation == b.generation && a.length == b.length && a.checksum == b.checksum &&
		   a.cursor == b.cursor && a.profile == b.profile;
}
std::string generation_name(const GenerationId &id) {
	std::array<char, 43> bytes;
	return std::string(generation_name(id, bytes));
}
std::string_view generation_name(const GenerationId &id, std::span<char> destination) {
	constexpr std::string_view prefix = "generation-";
	constexpr char hex[] = "0123456789abcdef";
	constexpr auto length = prefix.size() + std::tuple_size_v<GenerationId> * 2;
	if (destination.size() < length)
		throw std::length_error("generation name destination too small");
	std::copy(prefix.begin(), prefix.end(), destination.begin());
	auto offset = prefix.size();
	for (unsigned char c : id) {
		destination[offset++] = hex[c >> 4];
		destination[offset++] = hex[c & 15];
	}
	return {destination.data(), length};
}
std::optional<detail::Selected> detail::read_selected(int directory, uid_t owner) {
	File table_file(::openat(directory, "selected", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	if (table_file.fd < 0 && errno == ENOENT)
		throw GenerationUnavailable("the store has no selected generation");
	struct stat info{};
	require(table_file.fd >= 0 && ::fstat(table_file.fd, &info) == 0 && info.st_size > 0 &&
			std::uint64_t(info.st_size) <= table_bytes(1));
	trusted_file(table_file.fd, owner, std::uint64_t(info.st_size));
	std::vector<char> bytes(std::size_t(info.st_size));
	read_record(table_file.fd, bytes);
	StoreTable table;
	try {
		table = decode_table(bytes);
	} catch (const TableRefused &) {
		require(false);
	}
	require(table.entries.size() == 1);
	const auto &entry = table.entries.front();
	require(entry.root_length >= header_bytes);
	// The cursor is the image's: its leading bytes, the paged root or the flat header.
	const GenerationName image_name(entry.generation, GenerationFile::Image);
	std::array<char, 65> terminated{};
	std::copy(image_name.view().begin(), image_name.view().end(), terminated.begin());
	File image(::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	if (image.fd < 0 && errno == ENOENT)
		return std::nullopt;
	trusted_file(image.fd, owner, entry.root_length);
	std::array<char, paged::root_bytes> prefix{};
	const auto length = std::min<std::uint64_t>(entry.root_length, prefix.size());
	read_record(image.fd, std::span(prefix).first(length));
	Selected selected;
	auto &descriptor = selected.descriptor;
	if (std::string_view(prefix.data(), 8) == "DTROOT03") {
		require(entry.root_length == paged::root_bytes);
		std::optional<std::pair<distribution::Cursor, distribution::Profile>> root;
		try {
			root = validate_paged_root_prefix(prefix);
		} catch (const std::invalid_argument &) {
		}
		require(root.has_value());
		descriptor.cursor = root->first;
		require(root->second == table.profile);
		selected.paged = true;
	} else {
		const auto header = decode_header(std::span(prefix).first(length), entry.root_length);
		require(header.layout.length == entry.root_length && header.profile == table.profile);
		descriptor.cursor = header.cursor;
	}
	require(namespace_cursor(descriptor.cursor) == entry.cursor);
	descriptor.generation = entry.generation;
	descriptor.length = entry.root_length;
	descriptor.checksum = entry.root;
	descriptor.profile = table.profile;
	descriptor.ordinal = entry.ordinal;
	return selected;
}
namespace {
// The selection of an open directory: the table read again while the image it names moves on.
detail::Selected selection_of(int directory, uid_t owner) {
	for (unsigned attempt = 0; attempt < 3; ++attempt)
		if (auto selected = detail::read_selected(directory, owner))
			return *selected;
	throw RecoverySelectionChanged();
}
} // namespace
GenerationSelection read_generation_selection(const std::filesystem::path &directory) {
	File dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
	struct stat info{};
	require(dir.fd >= 0 && ::fstat(dir.fd, &info) == 0 && S_ISDIR(info.st_mode) && !(info.st_mode & 0022));
	return {selection_of(dir.fd, info.st_uid).descriptor, {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
}
RecoveredGeneration detail::RecoveryEngine::recover_routed(const std::filesystem::path &directory,
														   RecoveryContext legacy,
														   RecoveryContext paged_context,
														   std::optional<distribution::Cursor> expected) {
	if (paged_context.admission)
		return recover(directory, std::move(paged_context), expected);
	for (unsigned attempt = 0; attempt < 3; ++attempt) {
		File dir(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
		struct stat info{};
		require(dir.fd >= 0 && ::fstat(dir.fd, &info) == 0 && S_ISDIR(info.st_mode) &&
				!(info.st_mode & 0022));
		const auto selected = detail::read_selected(dir.fd, info.st_uid);
		if (!selected)
			continue;
		const auto &descriptor = selected->descriptor;
		const auto name = generation_name(descriptor.generation) + ".lease";
		File lease(::openat(dir.fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
		if (lease.fd < 0 && errno == ENOENT)
			continue;
		require(lease.fd >= 0);
		if (::flock(lease.fd, LOCK_SH | LOCK_NB) != 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			require(false);
		}
		struct stat held{}, named{};
		require(::fstat(lease.fd, &held) == 0);
		if (!held.st_nlink)
			continue;
		trusted_file(lease.fd, info.st_uid, 0);
		if (::fstatat(dir.fd, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				continue;
			require(false);
		}
		if (held.st_dev != named.st_dev || held.st_ino != named.st_ino)
			continue;
		auto context = selected->paged ? paged_context : legacy;
		context.selection =
			GenerationSelection{descriptor, {std::uint64_t(info.st_dev), std::uint64_t(info.st_ino)}};
		context.paged = selected->paged;
		try {
			return recover(directory, std::move(context), expected);
		} catch (const RecoverySelectionChanged &) {
		}
	}
	throw RecoverySelectionChanged();
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected, nullptr);
}
RecoveredGeneration recover_generation(const std::filesystem::path &directory, ViewLimits limits,
									   std::optional<distribution::Cursor> expected,
									   const RecoveredGeneration *prior) {
	auto &domain = resources::domain(limits);
	detail::RecoveryContext context{domain.factory(),
									domain.storage_account(),
									resources::mapping_account(limits),
									limits,
									prior ? prior->paged_
										  : distribution::RetirementHandle<detail::LeasedPagedRoot>{},
									{}};
	return detail::RecoveryEngine::recover(directory, std::move(context), expected);
}
RecoveredGeneration detail::RecoveryEngine::recover(const std::filesystem::path &directory,
													RecoveryContext context,
													std::optional<distribution::Cursor> expected) {
	bool claimed = false;
	try {
		auto limits = context.limits;
		limits.retirement_domain = nullptr;
		limits.mapping_account = context.mappings;
		if (!context.heap || !context.mappings || !context.factory.uses_account(context.heap) ||
			!context.budget.bytes || !context.budget.rows)
			throw std::invalid_argument("recovery context requires exact ledgers");
		auto factory = context.factory;
		const auto &prior = context.prior;
		const auto sample = [&context] {
			if (context.metrics) {
				context.metrics->sampled_heap_peak =
					std::max(context.metrics->sampled_heap_peak, std::uint64_t(context.heap->used()));
				context.metrics->sampled_mapping_peak =
					std::max(context.metrics->sampled_mapping_peak, std::uint64_t(context.mappings->used()));
			}
		};
		sample();
		if (context.admission) {
			if (auto failure = context.admission->failure())
				std::rethrow_exception(failure);
			context.admission->bind_resources(context.heap, context.mappings, prior);
			const auto selection = read_generation_selection(directory);
			context.admission->bind_selection(selection.descriptor, selection.directory_identity);
		}
		std::shared_ptr<File> dir;
		if (context.admission) {
			context.admission->claim_attempt();
			claimed = true;
			dir = context.admission->take_directory();
		} else {
			auto directory_owner = factory.make<Directory>();
			using DirectoryLease = distribution::RetirementHandle<Directory>;
			auto directory_lease = std::allocate_shared<DirectoryLease>(
				distribution::StorageAllocator<DirectoryLease>(context.heap), directory_owner);
			dir = {directory_lease, &directory_owner->file};
		}
		dir->fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		struct stat directory_info{};
		require(dir->fd >= 0 && ::fstat(dir->fd, &directory_info) == 0 && S_ISDIR(directory_info.st_mode) &&
				!(directory_info.st_mode & 0022));
		for (unsigned attempt = 0; attempt < (context.admission ? 1u : 3u); ++attempt) {
			const auto selected = detail::read_selected(dir->fd, directory_info.st_uid);
			if (!selected) {
				if (context.selection)
					throw RecoverySelectionChanged();
				continue;
			}
			const auto descriptor = selected->descriptor;
			if (context.selection && (context.selection->descriptor != descriptor ||
									  context.selection->directory_identity !=
										  DirectoryIdentity{std::uint64_t(directory_info.st_dev),
															std::uint64_t(directory_info.st_ino)}))
				throw RecoverySelectionChanged();
			require(descriptor.length <= limits.bytes);
			if (context.admission)
				context.admission->bind_selection(
					descriptor, {std::uint64_t(directory_info.st_dev), std::uint64_t(directory_info.st_ino)});
			auto name = generation_name(descriptor.generation);
			const auto mapping_account = context.mappings;
			auto mapping =
				context.admission ? context.admission->take_root() : factory.make<Mapping>(mapping_account);
			if (context.admission)
				require(mapping->account == mapping_account && mapping->length == descriptor.length);
			mapping->lease = ::openat(dir->fd, (name + ".lease").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
			if (mapping->lease < 0 && errno == ENOENT)
				continue;
			require(mapping->lease >= 0);
			if (::flock(mapping->lease, LOCK_SH | LOCK_NB) != 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN)
					continue;
				require(false);
			}
			struct stat lease_info{};
			require(::fstat(mapping->lease, &lease_info) == 0);
			if (lease_info.st_nlink == 0)
				continue;
			trusted_file(mapping->lease, directory_info.st_uid, 0);
			// A collector may have unlinked a lease between open and locking.
			// Match the held inode against the name before opening its image.
			struct stat named_lease{};
			if (::fstatat(dir->fd, (name + ".lease").c_str(), &named_lease, AT_SYMLINK_NOFOLLOW) != 0) {
				if (errno == ENOENT)
					continue;
				require(false);
			}
			if (named_lease.st_dev != lease_info.st_dev || named_lease.st_ino != lease_info.st_ino)
				continue;
			if (context.paged && *context.paged != selected->paged)
				throw RecoverySelectionChanged();
			mapping->image = ::openat(dir->fd, (name + ".image").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
			trusted_file(mapping->image, directory_info.st_uid, descriptor.length);
			mapping->length = descriptor.length;
			if (!context.admission)
				mapping->ticket = mapping->account->admit(mapping_capacity(descriptor.length));
			mapping->address = ::mmap(nullptr, mapping->length, PROT_READ, MAP_SHARED, mapping->image, 0);
			require(mapping->address != MAP_FAILED);
			if (context.metrics)
				++context.metrics->root_mapping_installs;
			auto bytes = std::span(static_cast<const char *>(mapping->address), mapping->length);
			SHA256 hash;
			for (std::size_t at = 0; at < bytes.size();) {
				auto count = std::min<std::size_t>(65536, bytes.size() - at);
				hash.add(bytes.data() + at, count);
				if (context.metrics)
					context.metrics->root_hash_bytes += count;
				at += count;
			}
			unsigned char digest[32];
			hash.getHash(digest);
			require(std::memcmp(digest, descriptor.checksum.data(), 32) == 0);
			if (selected->paged) {
				const auto bounds = detail::paged_limits(limits, descriptor.length);
				const auto root = paged::decode_root(bytes, bounds);
				require(root.cursor == descriptor.cursor && root.profile == descriptor.profile &&
						(!expected || (expected->same_identity(root.cursor) &&
									   root.cursor.revision >= expected->revision)));
				auto account = context.heap;
				const DirectoryIdentity identity{std::uint64_t(directory_info.st_dev),
												 std::uint64_t(directory_info.st_ino)};
				if (context.admission)
					context.admission->bind(descriptor, identity);
				const bool warm =
					detail::eligible_warm_recovery(prior, root, identity, account, mapping_account);
				if (context.admission)
					require(context.admission->warm() == warm);
				const bool progress = warm && detail::reuses_paged_state(prior->root(), root);
				if (context.metrics) {
					context.metrics->warm = warm;
					context.metrics->progress = progress;
				}
				std::optional<detail::WarmGraphValidator> warm_validator;
				std::optional<detail::DependencyDiscovery> discovery;
				distribution::RetirementHandle<detail::SegmentRegistry> registry;
				// A generation with a live record is trusted: a cold start trusts what its build verified, and
				// the recovery after this process's own publication trusts the build it just ran.
				std::optional<detail::TrustedGeneration> trusted;
				if (context.trusted_start && !progress)
					trusted = detail::TrustedRestart::open(
						dir->fd, directory_info.st_uid, descriptor, root, bounds, limits, factory, account,
						mapping_account, context.metrics.get(),
						{context.admission.get(), warm ? prior->registry().get() : nullptr});
				if (trusted) {
					registry = trusted->registry;
				} else if (progress) {
					registry = prior->registry();
					const auto counts = prior->counts();
					require(counts.strings <= limits.strings &&
							counts.maximum_overrides <= limits.overrides &&
							registry->physical_bytes() <= bounds.bytes);
				} else {
					discovery.emplace(
						account, factory, root, bounds,
						warm ? prior->registry() : distribution::RetirementHandle<detail::SegmentRegistry>{},
						context.admission ? (warm ? context.admission->fresh_registry()
												  : context.admission->target_registry())
										  : distribution::RetirementHandle<detail::SegmentRegistry>{});
					while (!discovery->done()) {
						auto budget = context.budget;
						discovery->step(budget);
						sample();
						if (auto missing = discovery->missing()) {
							const detail::SegmentNames names(*missing);
							File record(
								::openat(dir->fd, names.ownership.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
							trusted_file(record.fd, directory_info.st_uid, segment_ownership_bytes);
							std::array<char, segment_ownership_bytes> record_bytes;
							read_record(record.fd, record_bytes);
							const auto ownership = decode_segment_ownership(record_bytes);
							require(ownership.segment == *missing);
							File payload(
								::openat(dir->fd, names.payload.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
							auto segment =
								context.admission
									? context.admission->take_segment(ownership)
									: factory.make<detail::SegmentMapping>(mapping_account, payload.fd,
																		   directory_info.st_uid, ownership);
							if (context.admission)
								segment->install(payload.fd, directory_info.st_uid);
							if (context.metrics)
								++context.metrics->segment_mapping_installs;
							sample();
							discovery->supply(std::move(segment));
						}
					}
					registry = discovery->take();
				}

				if (warm && !progress && !trusted) {
					using Registry = distribution::RetirementHandle<detail::SegmentRegistry>;
					auto fresh = registry;
					std::shared_ptr<const void> owner =
						context.admission ? context.admission->fresh_owner()
										  : std::allocate_shared<Registry>(
												distribution::StorageAllocator<Registry>(account), fresh);
					warm_validator.emplace(account, prior, owner, fresh->inputs(), root, bounds,
										   detail::PagedSemanticLimits{limits.strings, limits.overrides});
					while (!warm_validator->done()) {
						auto budget = context.budget;
						warm_validator->step(budget);
						sample();
					}
					detail::WarmRegistryMerge merge(
						account, factory, prior, fresh, *warm_validator, bounds,
						context.admission ? context.admission->target_registry()
										  : distribution::RetirementHandle<detail::SegmentRegistry>{});
					while (!merge.done()) {
						auto budget = context.budget;
						merge.step(budget);
						sample();
					}
					registry = merge.take();
				}

				using RootLease = distribution::RetirementHandle<Mapping>;
				std::shared_ptr<const void> root_lease =
					context.admission ? context.admission->root_lease()
									  : std::allocate_shared<RootLease>(
											distribution::StorageAllocator<RootLease>(account), mapping);
				auto capability = context.admission
									  ? context.admission->capability()
									  : factory.make<detail::LeasedPagedRoot>(
											dir, root_lease, registry, descriptor, identity, root);
				using Capability = distribution::RetirementHandle<detail::LeasedPagedRoot>;
				std::optional<detail::GraphValidator> validator;
				if (!warm && !trusted) {
					std::shared_ptr<const void> owner =
						context.admission
							? context.admission->capability_owner()
							: std::allocate_shared<Capability>(
								  distribution::StorageAllocator<Capability>(account), capability);
					validator.emplace(account, owner, registry->inputs(), root, bounds,
									  detail::PagedSemanticLimits{limits.strings, limits.overrides},
									  registry.get());
					while (!validator->done()) {
						auto budget = context.budget;
						validator->step(budget);
						sample();
					}
					for (;;) {
						auto budget = context.budget;
						if (validator->record_reachability(*registry, budget))
							break;
					}
				}
				for (std::size_t i = 0; i < registry->inputs().size(); ++i) {
					const auto &segment = registry->mapping(i);
					const detail::SegmentNames names(segment->ownership().segment);
					File record(::openat(dir->fd, names.ownership.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
					trusted_file(record.fd, directory_info.st_uid, segment_ownership_bytes);
					std::array<char, segment_ownership_bytes> record_bytes;
					read_record(record.fd, record_bytes);
					require(decode_segment_ownership(record_bytes) == segment->ownership());
					struct stat named{};
					require(::fstatat(dir->fd, names.payload.data(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
							S_ISREG(named.st_mode) && named.st_uid == directory_info.st_uid &&
							named.st_nlink == 1 && !(named.st_mode & 0022) && named.st_size >= 0 &&
							std::uint64_t(named.st_size) == segment->ownership().length &&
							detail::SegmentFileIdentity{std::uint64_t(named.st_dev),
														std::uint64_t(named.st_ino)} == segment->identity());
				}
				trusted_file(mapping->image, directory_info.st_uid, descriptor.length);
				trusted_file(mapping->lease, directory_info.st_uid, 0);
				struct stat held_image{}, named_image{}, named_anchor{};
				require(
					::fstat(mapping->image, &held_image) == 0 &&
					::fstatat(dir->fd, (name + ".image").c_str(), &named_image, AT_SYMLINK_NOFOLLOW) == 0 &&
					held_image.st_dev == named_image.st_dev && held_image.st_ino == named_image.st_ino &&
					::fstatat(dir->fd, (name + ".lease").c_str(), &named_anchor, AT_SYMLINK_NOFOLLOW) == 0 &&
					lease_info.st_dev == named_anchor.st_dev && lease_info.st_ino == named_anchor.st_ino);
				if (progress)
					capability->seal_progress(*prior);
				else
					capability->seal(trusted ? trusted->counts : warm ? warm_validator->counts() : validator->counts());
				if (context.metrics) {
					if (discovery)
						context.metrics->segment_hash_bytes += discovery->hashed_segment_bytes();
					if (warm_validator) {
						context.metrics->segment_hash_bytes += warm_validator->hashed_segment_bytes();
						context.metrics->definitions_validated += warm_validator->definitions_validated();
						context.metrics->old_definition_headers += warm_validator->old_definition_headers();
					}
					if (validator) {
						context.metrics->segment_hash_bytes += validator->hashed_segment_bytes();
						context.metrics->definitions_validated += root.records;
					}
				}
				if (warm_validator)
					for (;;) {
						auto budget = context.budget;
						if (warm_validator->discard(budget))
							break;
					}
				if (discovery)
					for (;;) {
						auto budget = context.budget;
						if (discovery->discard(budget))
							break;
					}
				sample();
				auto view = context.admission ? context.admission->take_view()
											  : detail::PagedProgressView::take(factory, account, capability);
				auto result =
					RecoveredGeneration(std::move(view), descriptor, dir, identity, std::move(capability));
				if (context.admission)
					context.admission->complete_attempt();
				return result;
			}
			auto header = decode_header(bytes, limits.bytes);
			require(header.cursor == descriptor.cursor && header.profile == descriptor.profile);
			using Lease = distribution::RetirementHandle<Mapping>;
			auto lease =
				std::allocate_shared<Lease>(distribution::StorageAllocator<Lease>(context.heap), mapping);
			ImageValidator validator(lease, bytes, limits, expected, factory, context.heap);
			while (!validator.done()) {
				auto budget = context.budget;
				validator.step(budget);
			}
			return RecoveredGeneration(
				validator.take(), descriptor, dir,
				{std::uint64_t(directory_info.st_dev), std::uint64_t(directory_info.st_ino)});
		}
		throw std::runtime_error("local generation changed while acquiring its lease");
	} catch (...) {
		if (claimed)
			context.admission->fail_attempt(std::current_exception());
		throw;
	}
}

ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits,
						 std::optional<distribution::Cursor> expected) {
	return recover_generation(directory, limits, expected).view();
}
} // namespace detent::local
