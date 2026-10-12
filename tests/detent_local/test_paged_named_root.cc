#include "owned_segment_inputs.hh"
#include "paged_disk_fixture.hh"
#include "paged_named_root.hh"
#include "paged_segment_read.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
namespace c = kronuz::io::completion;
void drive(l::detail::PagedNamedRoot &reader) {
	for (unsigned turns = 0; !reader.done(); ++turns) {
		require(turns < 1000);
		reader.submitted();
		require(reader.complete(kronuz::journal::detail::execute_primitive(reader.io(), reader.request())));
	}
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-named-root-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image, storage);
	auto account = std::make_shared<d::StorageLedger>(32 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	l::ViewLimits limits;
	limits.retirement_domain = &domain;
	limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	{
		auto certificate = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		{
			auto actual = l::recover_generation(directory, limits);
			certificate->capture(actual);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (limits.mapping_account->used()) {
			require(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		require(std::filesystem::remove(directory / "selected"));
		auto other_root = image.root();
		++other_root.cursor.revision;
		const auto other_bytes = l::paged::encode_root(other_root);
		l::GenerationId other_generation;
		other_generation.fill('n');
		const l::Descriptor other_descriptor{other_generation, l::paged::root_bytes,
											 l::paged::physical_commitment(other_bytes), other_root.cursor,
											 other_root.profile};
		const auto other_name = l::generation_name(other_generation);
		write(directory / (other_name + ".image"), other_bytes);
		write(directory / (other_name + ".lease"),
			  l::encode_generation_ownership(
				  {storage, other_generation, other_descriptor.length, other_descriptor.checksum, 2}));
		write(directory / "selected", l::encode_descriptor(other_descriptor));
		auto other = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		{
			auto actual = l::recover_generation(directory, limits);
			other->capture(actual);
		}
		require(std::filesystem::remove(directory / "selected"));
		const auto other_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (limits.mapping_account->used()) {
			require(std::chrono::steady_clock::now() < other_deadline);
			std::this_thread::yield();
		}
		auto session = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
		const auto name = l::generation_name(generation);
		const auto image_path = directory / (name + ".image");
		const auto lease_path = directory / (name + ".lease");
		const auto original = std::span<const char>(image.bytes()->data(), image.bytes()->size());
		const l::GenerationOwnership ownership{storage, generation, image.descriptor().length,
											   image.descriptor().checksum, 2};
		for (unsigned variant = 0; variant < 8; ++variant) {
			write(image_path, original);
			write(lease_path, l::encode_generation_ownership(ownership));
			if (variant == 2) {
				std::array<char, l::paged::root_bytes> corrupt;
				std::copy(original.begin(), original.end(), corrupt.begin());
				corrupt[56] ^= 1;
				write(image_path, corrupt);
			}
			l::detail::PagedNamedRoot reader(
				domain.factory(), session, storage, generation, ::getuid() + (variant == 3),
				variant == 1   ? d::RetirementHandle<l::detail::PagedDependencyCertificate>{}
				: variant == 7 ? other
							   : certificate);
			bool fired = false;
			const auto injected =
				std::make_exception_ptr(std::runtime_error("named root read cancelled with error"));
			for (unsigned turns = 0; !reader.done(); ++turns) {
				require(turns < 1000);
				const auto &request = reader.request();
				const bool root_read = request.kind == c::Kind::Read && request.source == name + ".image";
				reader.submitted();
				c::Result result;
				if (variant == 6 && root_read && !fired) {
					reader.close();
					result.token = request.token;
					result.error = injected;
					fired = true;
				} else {
					result = kronuz::journal::detail::execute_primitive(reader.io(), request);
					if (root_read && !fired && variant == 4) {
						auto changed = ownership;
						changed.image_checksum[0] ^= 1;
						write(lease_path, l::encode_generation_ownership(changed));
						fired = true;
					} else if (root_read && !fired && variant == 5) {
						const auto replacement = directory / "replacement.image";
						write(replacement, original);
						std::filesystem::rename(replacement, image_path);
						fired = true;
					}
				}
				c::Result stale;
				stale.token = result.token;
				++stale.token.step;
				require(!reader.complete(std::move(stale)) && reader.in_flight());
				require(reader.complete(std::move(result)));
			}
			if (variant < 2 || variant == 7) {
				require(reader.ready() && !reader.error() && reader.descriptor() == image.descriptor());
				require(bool(reader.certificate()) == (variant == 0));
				const int fd = ::open(lease_path.c_str(), O_RDWR | O_CLOEXEC);
				require(fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) != 0 &&
						(errno == EWOULDBLOCK || errno == EAGAIN));
				require(::close(fd) == 0);
				if (variant == 1) {
					l::detail::OwnedSegmentInputs inputs(domain.factory(), account, storage,
														 reader.root().segments,
														 16 * l::paged::segment_bytes);
					const auto owner = inputs.owner();
					l::detail::PagedDependencyWalk walk(account, owner, l::detail::OwnedSegmentInputs::lookup,
														reader.root(), storage);
					for (unsigned turns = 0; !walk.done(); ++turns) {
						require(turns < 100000);
						l::ValidationBudget budget;
						walk.step(budget);
						if (const auto id = walk.missing()) {
							l::detail::PagedSegmentRead pack(domain.factory(), session, account, storage, *id,
															 ::getuid());
							for (unsigned steps = 0; !pack.done(); ++steps) {
								require(steps < 100000);
								if (pack.needs_work()) {
									l::ValidationBudget work;
									pack.step(work);
								} else {
									pack.submitted();
									require(pack.complete(kronuz::journal::detail::execute_primitive(
										pack.io(), pack.request())));
								}
							}
							require(pack.ready() && !pack.error());
							inputs.begin(pack.ownership(), pack.bytes());
							while (inputs.busy()) {
								l::ValidationBudget work{1, 1};
								inputs.step(work);
							}
							pack.close();
							while (!pack.done()) {
								pack.submitted();
								require(pack.complete(
									kronuz::journal::detail::execute_primitive(pack.io(), pack.request())));
							}
						}
					}
					inputs.freeze();
					l::detail::GraphValidator graph(account, owner, inputs.inputs(), reader.root(), storage);
					while (!graph.done()) {
						l::ValidationBudget budget;
						graph.step(budget);
					}
					auto checked =
						domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
					checked->begin_named(reader, graph);
					l::ValidationBudget zero{0, 0};
					checked->capture_step(zero);
					require(!checked->sealed());
					while (!checked->sealed()) {
						l::ValidationBudget budget{1, 1};
						checked->capture_step(budget);
					}
					require(checked->descriptor() == reader.descriptor() &&
							checked->dependencies().size() == image.segments().size());
					require(checked->dependencies()[0] == image.segments()[0].ownership &&
							limits.mapping_account->used() == 0);
					for (;;) {
						l::ValidationBudget budget;
						if (graph.discard(budget))
							break;
					}
					for (;;) {
						l::ValidationBudget budget;
						if (walk.discard(budget))
							break;
					}
				}
			} else {
				require(!reader.ready() && reader.error());
				if (variant >= 4)
					require(fired);
				if (variant == 6)
					require(reader.error() == injected);
			}
			reader.close();
			drive(reader);
			require(reader.done() && !reader.ready() && !reader.in_flight());
			const int fd = ::open(lease_path.c_str(), O_RDWR | O_CLOEXEC);
			require(fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0);
			require(::close(fd) == 0 && limits.mapping_account->used() == 0);
		}
		{
			l::GenerationId legacy;
			legacy.fill('v');
			l::ImageBuilder builder(source, legacy);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			const auto descriptor = builder.descriptor();
			const auto name = l::generation_name(legacy);
			write(
				directory / (name + ".lease"),
				l::encode_generation_ownership({storage, legacy, descriptor.length, descriptor.checksum, 1}));
			write(directory / (name + ".image"), {builder.bytes()->data(), builder.bytes()->size()});
			l::detail::PagedNamedRoot reader(domain.factory(), session, storage, legacy, ::getuid(),
											 certificate);
			drive(reader);
			require(reader.ready() && !reader.paged() && !reader.error() && !reader.certificate());
			require(reader.descriptor() == descriptor);
			bool refused = false;
			try {
				reader.root();
			} catch (const std::logic_error &) {
				refused = true;
			}
			require(refused);
			reader.close();
			drive(reader);
			std::string enlarged(builder.bytes()->view());
			enlarged.append(17, 'x');
			write(directory / (name + ".image"), enlarged);
			write(directory / (name + ".lease"),
				  l::encode_generation_ownership(
					  {storage, legacy, enlarged.size(), l::paged::physical_commitment(enlarged), 1}));
			l::detail::PagedNamedRoot inconsistent(domain.factory(), session, storage, legacy, ::getuid());
			drive(inconsistent);
			require(!inconsistent.ready() && inconsistent.error());
			write(directory / (name + ".image"), builder.bytes()->view());
			write(directory / (name + ".lease"), std::span<const char>{});
			l::detail::PagedNamedRoot unmanaged(domain.factory(), session, storage, legacy, ::getuid());
			drive(unmanaged);
			require(unmanaged.ready() && unmanaged.legacy_anchor() && !unmanaged.paged() &&
					!unmanaged.certificate() && unmanaged.descriptor().length == descriptor.length &&
					unmanaged.descriptor().cursor == descriptor.cursor);
			unmanaged.close();
			drive(unmanaged);
		}
		session->close();
	}
	domain.close().wait();
	require(account->used() == initial && limits.mapping_account->used() == 0);
}
