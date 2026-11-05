#pragma once
#include "dense_graph_fixture.hh"
#include "detent_local/paged_definition.hh"
#include "names.hh"
#include "prepared_reader.hh"
#include "segment_names.hh"
#include "trusted_records.hh"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <source_location>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace prepared_test {
namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
namespace x = l::detail;
inline void check(bool value, std::source_location at = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("lazy page invariant at " + std::to_string(at.line()));
}
inline void write_file(const std::filesystem::path &path, std::span<const char> bytes) {
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	check(fd >= 0);
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
		if (written < 0 && errno == EINTR)
			continue;
		check(written > 0);
		offset += written;
	}
	check(::close(fd) == 0);
}
struct Fixture {
	std::filesystem::path path;
	int directory = -1;
	std::shared_ptr<d::StorageLedger> heap =
	    std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
	l::StorageIdentity storage{};
	l::GenerationId generation{};
	p::Root root;
	std::vector<p::Reference> definitions;
	std::vector<std::shared_ptr<const d::OwnedBuffer>> buffers;
	std::vector<x::SegmentInput> segments;
	std::uint64_t physical_bytes = p::root_bytes;
	explicit Fixture(unsigned count = 2) : definitions(count) {
		path = std::filesystem::current_path() / ".scratch" /
		       ("lazy-pages-" + std::to_string(::getpid()) + "-" + std::to_string(count));
		std::filesystem::create_directories(path.parent_path());
		check(::mkdir(path.c_str(), 0700) == 0);
		directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		check(directory >= 0);
		storage.fill('s');
		generation.fill('g' + count);
		auto packed = std::make_shared<dense_graph_test::Fixture>();
		packed->account = heap;
		packed->storage = storage;
		packed->segment.fill('s' + count);
		std::array<char, 202> key{};
		for (unsigned i = 0; i != count; ++i) {
			const std::string ns(1, 'a' + i);
			std::vector<char> watermark(8);
			l::write_integer(watermark, 0, 9, 8);
			auto length = p::encode_namespace_key(key, ns);
			packed->rows.push_back({std::string(key.data(), length), std::move(watermark)});
			detent::BoolDef boolean;
			boolean.default_value = true;
			boolean.overrides.emplace("force", false);
			const detent::KnobDef source =
			    i ? detent::KnobDef(boolean) : detent::KnobDef(detent::IntDef{42});
			std::vector<char> body(p::definition_size(source));
			p::encode_definition(body, source, 8);
			length = p::encode_definition_key(key, ns + ":k");
			packed->rows.push_back({std::string(key.data(), length), std::move(body)});
		}
		packed = dense_graph_test::seal(std::move(packed), count, count);
		root = packed->root;
		for (auto &reference : definitions)
			reference = root.tree;
		auto bytes = d::make_owned_buffer(heap, packed->bytes.size());
		bytes->append({packed->bytes.data(), packed->bytes.size()});
		bytes->seal();
		const auto ownership = packed->inputs[0].ownership;
		const x::SegmentNames names(ownership.segment);
		write_file(path / names.ownership.data(), l::encode_segment_ownership(ownership));
		write_file(path / names.payload.data(), {bytes->data(), bytes->size()});
		segments.push_back({ownership, {bytes->data(), bytes->size()}});
		buffers.push_back(bytes);
		physical_bytes += bytes->size();
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
		x::GraphValidator proof(heap, buffers[0], segments, root, storage);
		while (!proof.done()) {
			l::ValidationBudget budget;
			proof.step(budget);
		}
		const auto image = p::encode_root(root);
		const auto checksum = p::metadata_commitment(image);
		l::Descriptor descriptor{generation, p::root_bytes, checksum, root.cursor, root.profile};
		write_file(path / "storage.meta", l::encode_storage_identity(storage));
		write_file(path / x::GenerationName(generation, x::GenerationFile::Image).view(), image);
		write_file(
		    path / x::GenerationName(generation, x::GenerationFile::Lease).view(),
		    l::encode_generation_ownership({storage, generation, p::root_bytes, checksum, 3}));
		x::ReaderMetadata metadata{storage, generation, checksum, proof.counts(), physical_bytes};
		write_file(path / x::GenerationName(generation, ".read").view(),
		           x::encode_reader_metadata(metadata));
		write_file(path / "selected", l::encode_descriptor(descriptor));
		trusted_records::derive(path);
	}
	~Fixture() {
		if (directory >= 0)
			::close(directory);
		std::filesystem::remove_all(path);
	}
};
} // namespace prepared_test
