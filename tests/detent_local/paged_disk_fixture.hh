#pragma once
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "snapshot_fixture.hh"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
namespace l = detent::local;
inline void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), bytes.size());
	out.close();
	require(bool(out));
	require(::chmod(path.c_str(), 0600) == 0);
}
inline void install(const std::filesystem::path &directory, const l::detail::PagedImage &image,
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
