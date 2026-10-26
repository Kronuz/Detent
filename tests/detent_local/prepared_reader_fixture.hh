#pragma once
#include "names.hh"
#include "paged_segment_builder.hh"
#include "prepared_reader.hh"
#include "segment_names.hh"
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
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
	l::StorageIdentity storage{};
	l::GenerationId generation{};
	p::Root root;
	std::vector<p::Reference> definitions;
	std::vector<std::shared_ptr<const d::OwnedBuffer>> buffers;
	std::vector<x::SegmentInput> segments;
	std::uint64_t physical_bytes = p::root_bytes;
	explicit Fixture(unsigned count = 2) : definitions(count) {
		path = std::filesystem::current_path() /
		       ("lazy-pages-" + std::to_string(::getpid()) + "-" + std::to_string(count));
		check(::mkdir(path.c_str(), 0700) == 0);
		directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		check(directory >= 0);
		storage.fill('s');
		generation.fill('g' + count);
		std::vector<p::Reference> namespaces(count);
		auto append = [&](x::SegmentBuilder &segment, p::PageKind kind, std::string_view prefix,
		                  std::span<const p::Child> children, std::span<const char> body) {
			x::PageBuilder page(heap, kind,
			                    children.empty() ? 1 : (children[0].label == 'K' ? count * 2 : count), prefix,
			                    children, body);
			while (!page.done()) {
				l::ValidationBudget budget;
				page.step(budget);
			}
			segment.append(page);
			while (!segment.page_ready()) {
				l::ValidationBudget budget;
				segment.step(budget);
			}
			const auto reference = segment.reference();
			root.bytes += reference.length;
			++root.pages;
			return reference;
		};
		auto save = [&](x::SegmentBuilder &segment) {
			segment.seal();
			while (!segment.done()) {
				l::ValidationBudget budget;
				segment.step(budget);
			}
			const auto ownership = segment.ownership();
			const x::SegmentNames names(ownership.segment);
			write_file(path / names.ownership.data(), l::encode_segment_ownership(ownership));
			const auto bytes = segment.bytes();
			write_file(path / names.payload.data(), {bytes->data(), bytes->size()});
			segments.push_back({ownership, {bytes->data(), bytes->size()}});
			buffers.push_back(bytes);
			physical_bytes += bytes->size();
			++root.segments;
		};
		for (unsigned i = 0; i != count; ++i) {
			x::SegmentBuilder segment(heap, storage, generation, i);
			std::string ns(1, 'a' + i);
			ns += '\0';
			std::array<char, 8> watermark{};
			l::write_integer(watermark, 0, 9, 8);
			namespaces[i] =
			    append(segment, p::PageKind::Namespace, std::string_view(ns).substr(1), {}, watermark);
			detent::BoolDef boolean;
			boolean.default_value = true;
			boolean.overrides.emplace("force", false);
			const detent::KnobDef source = i ? detent::KnobDef(boolean) : detent::KnobDef(detent::IntDef{42});
			std::vector<char> body(p::definition_size(source));
			p::encode_definition(body, source, 8);
			definitions[i] = append(segment, p::PageKind::Definition, ns.substr(1) + 'k' + '\0', {}, body);
			save(segment);
		}
		x::SegmentBuilder index(heap, storage, generation, count);
		std::vector<p::Child> definitions_children, namespace_children;
		for (unsigned i = 0; i != count; ++i) {
			definitions_children.push_back({static_cast<std::uint8_t>('a' + i), definitions[i]});
			namespace_children.push_back({static_cast<std::uint8_t>('a' + i), namespaces[i]});
		}
		const auto definitions_root = append(index, p::PageKind::Branch, {}, definitions_children, {});
		const auto namespace_root = append(index, p::PageKind::Branch, {}, namespace_children, {});
		std::array<p::Child, 2> roots{{{'K', definitions_root}, {'W', namespace_root}}};
		root.tree = append(index, p::PageKind::Branch, {}, roots, {});
		root.logical = root.tree.logical;
		save(index);
		root.records = root.namespaces = count;
		root.cursor.cluster.fill('c');
		root.cursor.group.fill('g');
		root.cursor.restore_epoch.fill('e');
		root.cursor.scope.fill('s');
		root.cursor.revision = 10;
		std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
		x::GraphValidator proof(heap, buffers[0], segments, root, storage);
		while (!proof.done()) {
			l::ValidationBudget budget;
			proof.step(budget);
		}
		const auto image = p::encode_root(root);
		const auto checksum = p::physical_commitment(image);
		l::Descriptor descriptor{generation, p::root_bytes, checksum, root.cursor, root.profile};
		write_file(path / "storage.meta", l::encode_storage_identity(storage));
		write_file(path / x::GenerationName(generation, x::GenerationFile::Image).view(), image);
		write_file(path / x::GenerationName(generation, x::GenerationFile::Lease).view(),
		           l::encode_generation_ownership({storage, generation, p::root_bytes, checksum, 2}));
		x::ReaderMetadata metadata{storage, generation, checksum, proof.counts(), physical_bytes};
		write_file(path / x::GenerationName(generation, ".read").view(), x::encode_reader_metadata(metadata));
		write_file(path / "selected", l::encode_descriptor(descriptor));
	}
	~Fixture() {
		if (directory >= 0)
			::close(directory);
		std::filesystem::remove_all(path);
	}
};
} // namespace prepared_test
