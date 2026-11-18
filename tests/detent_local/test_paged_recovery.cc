#include "detent_local/reader.hh"
#include "paged_disk_fixture.hh"
#include "paged_snapshot_builder.hh"
#include "recovery_context.hh"
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
	l::GenerationId generation{};
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget b;
		builder.step(b);
	}
	auto image = builder.take();
	install(directory, image);
	write(directory / "unrelated.pack", std::string_view("unreachable garbage"));
	auto view = l::open_generation(directory);
	require(view.size() == 2 && view.get("app", "count").value() == INT64_MIN);
	require(view.get("app", "flag", "host", "beta").value() ==
			(detent::stable_bucket("app:flag", "host") < 17));
	{
		auto empty = image.root();
		empty.bytes = empty.pages = empty.segments = empty.records = empty.namespaces = 0;
		empty.tree = {};
		auto bytes = p::encode_root(empty, p::Limits{});
		auto descriptor = image.descriptor();
		descriptor.generation.fill('e');
		descriptor.checksum = p::metadata_commitment(bytes);
		trusted_records::stage(directory, descriptor, bytes);
		auto empty_view = l::open_generation(directory);
		require(empty_view.size() == 0 && !empty_view.contains("app", "count"));
		trusted_records::select(directory, image.descriptor());
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
	// A `.lease` that is not empty refuses the generation and keeps the installed view.
	auto descriptor = image.descriptor();
	descriptor.generation.fill('h');
	trusted_records::stage(directory, descriptor, image.bytes()->view());
	write(directory / (l::generation_name(descriptor.generation) + ".lease"), std::string_view("x"));
	rejects<std::runtime_error>([&] { reader->refresh(); });
	require(reader->read_view().get("app", "count").value() == INT64_MIN);
	reader.reset();
	std::filesystem::remove_all(directory);
	require(pinned.get("app", "count").value() == INT64_MIN && view.get("app", "count").value() == INT64_MIN);
}
