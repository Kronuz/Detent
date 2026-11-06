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
inline void check(bool value,
                  std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("lazy page invariant at " +
                             std::to_string(at.line()));
}
inline void write_file(const std::filesystem::path &path,
                       std::span<const char> bytes) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  check(fd >= 0);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto written =
        ::write(fd, bytes.data() + offset, bytes.size() - offset);
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
  explicit Fixture(unsigned count = 2, unsigned per_namespace = 1,
                   bool separate_segments = false)
      : definitions(count) {
    path = std::filesystem::current_path() / ".scratch" /
           ("lazy-pages-" + std::to_string(::getpid()) + "-" +
            std::to_string(count) + "-" + std::to_string(per_namespace));
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
      packed->rows.push_back(
          {std::string(key.data(), length), std::move(watermark)});
      for (unsigned j = 0; j != per_namespace; ++j) {
        detent::BoolDef boolean;
        boolean.default_value = true;
        boolean.overrides.emplace("force", false);
        const detent::KnobDef source =
            i ? detent::KnobDef(boolean)
              : detent::KnobDef(detent::IntDef{42 + j});
        std::vector<char> body(p::definition_size(source));
        p::encode_definition(body, source, 8);
        const auto name =
            ns + ":k" + (per_namespace == 1 ? "" : std::to_string(100000 + j));
        length = p::encode_definition_key(key, name);
        packed->rows.push_back(
            {std::string(key.data(), length), std::move(body)});
      }
    }
    packed =
        dense_graph_test::seal(std::move(packed), count * per_namespace, count);
    root = packed->root;
    std::vector<std::vector<char>> packs;
    std::vector<l::StorageIdentity> identities;
    if (separate_segments) {
      check(per_namespace == 1 && count >= 2 && count <= 26);
      packs.resize(count + 1);
      identities.resize(count + 1);
      for (unsigned i = 0; i <= count; ++i) {
        identities[i] = packed->segment;
        identities[i][0] = static_cast<char>(i + 1);
      }
      root.bytes = root.pages = 0;
      // Each namespace owns its leaves; shared trie branches occupy the index pack.
      auto scatter = [&](std::span<const char> source, std::size_t offset,
                         std::string path, auto &self) -> p::Reference {
        const auto node = p::decode_node(source, offset);
        path += node.prefix;
        std::vector<p::Reference> children;
        for (std::size_t i = 0; i < node.labels.size(); ++i) {
          const auto slot = node.slot(i);
          if (slot & 0x8000) {
            const auto child = p::decode_external(source, slot & 0x7fff);
            children.push_back(self(std::span<const char>(packed->bytes).subspan(child.offset, child.length),
                                    p::page_header_bytes, path + node.labels[i], self));
          } else {
            children.push_back(self(source, slot, path + node.labels[i], self));
          }
        }
        p::PageHeader header;
        header.nodes = 1;
        header.externals = children.size();
        header.keys = children.empty() ? 1 : 0;
        header.height = 1;
        std::vector<char> page(p::page_header_bytes);
        page.push_back(static_cast<char>(node.kind));
        page.push_back(static_cast<char>(node.prefix.size()));
        if (children.empty()) {
          page.resize(page.size() + 2);
          l::write_integer(page, page.size() - 2, node.body.size(), 2);
          page.insert(page.end(), node.prefix.begin(), node.prefix.end());
          page.insert(page.end(), node.body.begin(), node.body.end());
        } else {
          page.push_back(static_cast<char>(children.size() - 1));
          page.insert(page.end(), node.prefix.begin(), node.prefix.end());
          page.insert(page.end(), node.labels.begin(), node.labels.end());
          for (unsigned i = 0; i < children.size(); ++i) {
            const auto at = page.size();
            page.resize(at + 2);
            l::write_integer(page, at, 0x8000 | i, 2);
            header.keys += children[i].keys;
            header.height = std::max<unsigned>(header.height, children[i].height + 1);
          }
          for (const auto &child : children) {
            const auto content = p::encode_content(child);
            page.insert(page.end(), content.begin(), content.end());
          }
          for (const auto &child : children) {
            const auto placement = p::encode_placement(child);
            page.insert(page.end(), placement.begin(), placement.end());
          }
        }
        header.length = page.size();
        const auto encoded = p::encode_page_header(header);
        std::copy(encoded.begin(), encoded.end(), page.begin());
        const auto digest = p::page_commitment(page);
        std::copy(digest.begin(), digest.end(), page.begin() + 32);
        const unsigned group = path.size() >= 2 ? path[1] - 'a' : count;
        check(group <= count);
        auto &pack = packs[group];
        pack.resize((pack.size() + 7) & ~std::size_t(7));
        p::Reference reference{identities[group], static_cast<std::uint32_t>(pack.size()),
                               header.length, header.keys, header.height, digest};
        p::verify_page(page, reference);
        pack.insert(pack.end(), page.begin(), page.end());
        root.bytes += page.size();
        ++root.pages;
        if (node.kind == p::PageKind::Definition)
          definitions[group] = reference;
        return reference;
      };
      root.tree = scatter(std::span<const char>(packed->bytes).subspan(
                              packed->root.tree.offset, packed->root.tree.length),
                          p::page_header_bytes, {}, scatter);
      root.segments = packs.size();
    } else {
      for (auto &reference : definitions)
        reference = root.tree;
      packs.push_back(packed->bytes);
      identities.push_back(packed->segment);
    }
    for (unsigned i = 0; i < packs.size(); ++i) {
      auto bytes = d::make_owned_buffer(heap, packs[i].size());
      bytes->append(packs[i]);
      bytes->seal();
      const l::SegmentOwnership ownership{storage, identities[i], bytes->size(),
                                         p::pack_commitment(bytes->view())};
      const x::SegmentNames names(ownership.segment);
      write_file(path / names.ownership.data(),
                 l::encode_segment_ownership(ownership));
      write_file(path / names.payload.data(), {bytes->data(), bytes->size()});
      segments.push_back({ownership, {bytes->data(), bytes->size()}});
      buffers.push_back(bytes);
      physical_bytes += bytes->size();
    }
    std::sort(segments.begin(), segments.end(),
              [](const auto &a, const auto &b) {
                return std::memcmp(a.ownership.segment.data(),
                                   b.ownership.segment.data(), 16) < 0;
              });
    x::GraphValidator proof(heap, buffers[0], segments, root, storage);
    while (!proof.done()) {
      l::ValidationBudget budget;
      proof.step(budget);
    }
    const auto image = p::encode_root(root);
    const auto checksum = p::metadata_commitment(image);
    l::Descriptor descriptor{generation, p::root_bytes, checksum, root.cursor,
                             root.profile};
    write_file(path / "storage.meta", l::encode_storage_identity(storage));
    write_file(
        path / x::GenerationName(generation, x::GenerationFile::Image).view(),
        image);
    write_file(
        path / x::GenerationName(generation, x::GenerationFile::Lease).view(),
        l::encode_generation_ownership(
            {storage, generation, p::root_bytes, checksum, 3}));
    x::ReaderMetadata metadata{storage, generation, checksum, proof.counts(),
                               physical_bytes};
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
