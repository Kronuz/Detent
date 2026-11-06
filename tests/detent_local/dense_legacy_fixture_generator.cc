// Compile against Detent:785fb34 to regenerate the frozen version-2 fixture.
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
namespace l = detent::local;
void put(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream output(path, std::ios::binary);
	output.write(bytes.data(), bytes.size());
	output.close();
	require(bool(output) && ::chmod(path.c_str(), 0600) == 0);
}
int main(int argc, char **argv) {
	require(argc == 2);
	const std::filesystem::path directory(argv[1]);
	std::filesystem::create_directories(directory);
	require(::chmod(directory.c_str(), 0700) == 0);
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
	const auto &descriptor = image.descriptor();
	const auto name = l::generation_name(descriptor.generation);
	put(directory / "owner.lock", {});
	put(directory / "storage.meta", l::encode_storage_identity(storage));
	put(directory / "selected", l::encode_descriptor(descriptor));
	put(directory / (name + ".lease"), l::encode_generation_ownership(
		{storage, descriptor.generation, descriptor.length, descriptor.checksum, 2}));
	put(directory / (name + ".image"), {image.bytes()->data(), image.bytes()->size()});
	for (const auto &segment : image.segments()) {
		const l::detail::SegmentNames names(segment.ownership.segment);
		put(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
		put(directory / names.payload_name(), {segment.bytes->data(), segment.bytes->size()});
	}
	// Exact values certify the fixture before its bytes become a refusal oracle.
	const auto recovered = l::recover_generation(directory);
	require(recovered.view().size() == 2 &&
			recovered.view().get("app", "count").value() == INT64_MIN &&
			recovered.view().eval("app", "flag", "host", "beta").value() ==
				(detent::stable_bucket("app:flag", "host") < 17));
}
