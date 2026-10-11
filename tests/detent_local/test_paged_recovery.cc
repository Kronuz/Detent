#include "detent_local/reader.hh"
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
namespace l = detent::local;
namespace p = l::paged;
template <class Error, class F> void rejects(F action) {
	try {
		action();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged recovery accepted");
}
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), bytes.size());
	out.close();
	require(bool(out));
	require(::chmod(path.c_str(), 0600) == 0);
}
void install(const std::filesystem::path &directory, const l::detail::PagedImage &image,
			 const l::StorageIdentity &storage) {
	std::filesystem::create_directories(directory);
	require(::chmod(directory.c_str(), 0700) == 0);
	write(directory / "storage.meta", l::encode_storage_identity(storage));
	auto name = l::generation_name(image.descriptor().generation);
	l::GenerationOwnership ownership{storage, image.descriptor().generation, image.descriptor().length,
									 image.descriptor().checksum, 2};
	write(directory / (name + ".lease"), l::encode_generation_ownership(ownership));
	write(directory / (name + ".image"), {image.bytes()->data(), image.bytes()->size()});
	for (const auto &s : image.segments()) {
		const l::detail::SegmentNames names(s.ownership.segment);
		write(directory / names.owner_name(), l::encode_segment_ownership(s.ownership));
		write(directory / names.payload_name(), {s.bytes->data(), s.bytes->size()});
	}
	write(directory / "selected", l::encode_descriptor(image.descriptor()));
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-recovery-" + std::to_string(::getpid()));
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
		l::ValidationBudget b;
		builder.step(b);
	}
	auto image = builder.take();
	install(directory, image, storage);
	write(directory / "unrelated.pack", std::string_view("unreachable garbage"));
	auto view = l::open_generation(directory);
	require(view.size() == 2 && view.get("app", "count").value() == INT64_MIN);
	require(view.eval("app", "flag", "host", "beta").value() ==
			(detent::stable_bucket("app:flag", "host") < 17));
	{
		auto empty = image.root();
		empty.bytes = empty.pages = empty.segments = empty.records = empty.namespaces = 0;
		empty.tree = {};
		empty.logical = p::empty_commitment();
		auto bytes = p::encode_root(empty);
		auto descriptor = image.descriptor();
		descriptor.generation.fill('e');
		descriptor.checksum = p::physical_commitment(bytes);
		auto name = l::generation_name(descriptor.generation);
		write(directory / (name + ".image"), bytes);
		l::GenerationOwnership ownership{storage, descriptor.generation, descriptor.length,
										 descriptor.checksum, 2};
		write(directory / (name + ".lease"), l::encode_generation_ownership(ownership));
		write(directory / "selected", l::encode_descriptor(descriptor));
		auto empty_view = l::open_generation(directory);
		require(empty_view.size() == 0 && !empty_view.contains("app", "count"));
		write(directory / "selected", l::encode_descriptor(image.descriptor()));
	}
	l::ViewLimits limits;
	limits.strings = 23;
	rejects<std::invalid_argument>([&] { l::open_generation(directory, limits); });
	limits.strings = 24;
	require(l::open_generation(directory, limits).size() == 2);
	limits.overrides = 0;
	rejects<std::invalid_argument>([&] { l::open_generation(directory, limits); });
	auto future = cursor();
	++future.revision;
	rejects<std::runtime_error>([&] { l::open_generation(directory, {}, future); });
	limits = {};
	limits.bytes = image.bytes()->size();
	for (const auto &s : image.segments())
		limits.bytes += s.bytes->size();
	--limits.bytes;
	rejects<std::invalid_argument>([&] { l::open_generation(directory, limits); });
	{
		auto heap = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
		d::RetirementRuntime runtime{controls, 1};
		auto domain = runtime.domain(heap, 16);
		auto mapped = std::make_shared<d::StorageLedger>(2 * ::sysconf(_SC_PAGESIZE) - 1);
		limits = {};
		limits.retirement_domain = &domain;
		limits.mapping_account = mapped;
		rejects<d::StoragePressure>([&] { l::open_generation(directory, limits); });
		domain.close().wait();
		require(mapped->used() == 0);
	}
	auto reader = std::make_unique<l::GenerationReader>(directory);
	require(reader->refresh());
	auto pinned = reader->read_view();
	const auto &segment = image.segments().front();
	const l::detail::SegmentNames names(segment.ownership.segment);
	auto path = directory / names.payload_name();
	auto hidden = directory / "hidden";
	std::filesystem::rename(path, hidden);
	rejects<std::invalid_argument>([&] { l::open_generation(directory); });
	std::filesystem::rename(hidden, path);
	std::filesystem::create_hard_link(path, hidden);
	rejects<std::invalid_argument>([&] { l::open_generation(directory); });
	std::filesystem::remove(hidden);
	require(::chmod(path.c_str(), 0620) == 0);
	rejects<std::invalid_argument>([&] { l::open_generation(directory); });
	require(::chmod(path.c_str(), 0600) == 0);
	// Reject an unsupported selected root without replacing the installed view.
	auto descriptor = image.descriptor();
	descriptor.generation.fill('h');
	auto root_name = l::generation_name(descriptor.generation);
	write(directory / (root_name + ".image"), {image.bytes()->data(), image.bytes()->size()});
	l::GenerationOwnership bad{storage, descriptor.generation, descriptor.length, descriptor.checksum, 2};
	auto lease = l::encode_generation_ownership(bad);
	l::write_integer(lease, 56, 3, 4);
	auto hash = p::physical_commitment(std::span<const char>(lease).first(96));
	std::copy(hash.begin(), hash.end(), lease.begin() + 96);
	write(directory / (root_name + ".lease"), lease);
	write(directory / "selected", l::encode_descriptor(descriptor));
	rejects<std::invalid_argument>([&] { reader->refresh(); });
	require(reader->read_view().get("app", "count").value() == INT64_MIN);
	reader.reset();
	std::filesystem::remove_all(directory);
	require(pinned.get("app", "count").value() == INT64_MIN && view.get("app", "count").value() == INT64_MIN);
}
