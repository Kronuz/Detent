#include "dense_graph_fixture.hh"
#include "lazy_page_access.hh"
#include "paged_navigation.hh"
#include "paged_read_state.hh"
#include "paged_warm_validator.hh"
#include "prepared_reader_fixture.hh"
#include "segment_names.hh"
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>
using namespace dense_graph_test;
namespace {
template <class Action> void rejects_corruption(Action action) {
  try {
    action();
  } catch (const std::invalid_argument &) {
    return;
  } catch (const l::detail::ReadSourceFailure &) {
    return;
  }
  throw std::runtime_error("corrupt dense graph accepted");
}
bool same_counts(const l::detail::PagedStateCounts &a,
                 const l::detail::PagedStateCounts &b) {
  return a.records == b.records && a.overrides == b.overrides &&
         a.strings == b.strings && a.maximum_overrides == b.maximum_overrides;
}
template <class Validator> void bounded(Validator &validator) {
  for (std::size_t steps = 0; !validator.done(); ++steps) {
    check(steps < 2000000);
    l::ValidationBudget budget{1, 1};
    validator.step(budget);
    check(budget.bytes <= 1 && budget.rows <= 1);
  }
}
void warm_skips() {
  namespace x = l::detail;
  auto source = std::make_shared<prepared_test::Fixture>(2, 500);
  auto recovered = l::recover_generation(source->path);
  const auto &prior = x::RecoveredTesting::root(recovered);
  auto account = std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
  auto root = source->root;
  ++root.cursor.revision;
  {
    const auto before = p::hash_counts();
    x::WarmGraphValidator warm(account, recovered, source, {}, root);
    bounded(warm);
    check(same_counts(warm.counts(), prior.counts()));
    check(!warm.definitions_validated() && !warm.old_definition_headers());
    check(!warm.hashed_segment_bytes() && !warm.removed(0).pages);
    check(p::hash_counts() == before);
  }
  const auto original =
      source->segments[0].bytes.subspan(root.tree.offset, root.tree.length);
  auto bytes =
      std::make_shared<std::vector<char>>(original.begin(), original.end());
  l::SegmentOwnership ownership;
  ownership.storage = source->storage;
  ownership.segment.fill('z');
  ownership.length = bytes->size();
  ownership.checksum = p::pack_commitment(*bytes);
  root.tree.segment = ownership.segment;
  root.tree.offset = 0;
  root.segments = 2;
  std::array<x::SegmentInput, 1> added{{{ownership, *bytes}}};
  check(p::decode_page_header(*bytes, p::Limits{}).externals > 1);
  {
    const auto before = p::hash_counts();
    x::WarmGraphValidator warm(account, recovered, bytes, added, root);
    bounded(warm);
    check(same_counts(warm.counts(), prior.counts()));
    check(warm.definitions_validated() < root.records);
    check(warm.old_definition_headers() == warm.definitions_validated());
    check(warm.hashed_segment_bytes() == bytes->size());
    check(warm.removed(0).pages == 1 && warm.removed(0).bytes == bytes->size());
    check(warm.added(0).pages == 1 && warm.added(0).bytes == bytes->size());
    const auto after = p::hash_counts();
    check(after.page_calls == before.page_calls + 1 &&
          after.pack_calls == before.pack_calls + 1);
    check(after.metadata_calls == before.metadata_calls);
    std::array<x::SegmentInput, 2> merged{source->segments[0], added[0]};
    x::GraphValidator cold(account, bytes, merged, root, source->storage);
    bounded(cold);
    check(same_counts(cold.counts(), warm.counts()));
    check(cold.segment_reachability(0).pages + warm.removed(0).pages ==
          source->root.pages);
    check(p::state_commitment(cold.root()) ==
          p::state_commitment(source->root));
    x::PagedReadState state{root, merged};
    for (unsigned j = 0; j != 500; ++j) {
      const auto key = "k" + std::to_string(100000 + j);
      check(state.definition("a", key)->default_value() == 42 + j);
      check(state.definition("b", key)->default_value() == 1);
    }
  }
  for (unsigned field : {32u, 40u, 44u}) {
    bytes->assign(original.begin(), original.end());
    const auto header = p::decode_page_header(*bytes, p::Limits{});
    const auto content = bytes->size() - p::child_bytes * header.externals;
    if (field == 32)
      l::write_integer(*bytes, content + field,
                       p::decode_external(*bytes, 0, p::Limits{}).keys + 1, 8);
    else if (field == 40)
      l::write_integer(*bytes, content + field,
                       p::decode_external(*bytes, 0, p::Limits{}).length + 1, 4);
    else
      ++(*bytes)[content + field];
    root.tree.digest = p::page_commitment(*bytes, p::Limits{});
    std::copy(root.tree.digest.begin(), root.tree.digest.end(),
              bytes->begin() + 32);
    added[0].ownership.checksum = p::pack_commitment(*bytes);
    rejects_corruption([&] {
      x::WarmGraphValidator warm(account, recovered, bytes, added, root);
      bounded(warm);
    });
  }
  check(!account->used());
}

void fuzz_sample(std::uint64_t &seed) {
  namespace x = l::detail;
  auto random = [&] {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  static const auto original = fixture(256);
  auto source = std::make_shared<Fixture>(*original);
  const auto selected = random() % source->pages.size();
  const auto reference = source->pages[selected].reference;
  auto page = std::span<char>(source->bytes)
                  .subspan(reference.offset, reference.length);
  const auto flips = 1 + random() % 4;
  for (unsigned i = 0; i != flips; ++i)
    page[random() % page.size()] ^= static_cast<char>(1 + random() % 255);
  if (random() & 1) {
    try {
      const auto digest = p::page_commitment(page, p::Limits{});
      std::copy(digest.begin(), digest.end(), page.begin() + 32);
      if (reference == source->root.tree)
        source->root.tree.digest = digest;
    } catch (const std::invalid_argument &) {
    }
  }
  source->refresh();
  auto tolerate_corruption = [](auto action) {
    try {
      action();
    } catch (const std::invalid_argument &) {
    } catch (const x::ReadSourceFailure &) {
    }
  };
  // Exercise both first-touch verification and traversal after certified
  // starts.
  for (bool warmed : {false, true}) {
    x::VerifiedPages verified(source->bytes.size(), source->account);
    if (warmed)
      for (const auto &entry : source->pages)
        verified.set(entry.reference.offset);
    source->inputs[0].pages = &verified;
    x::PagedReadState state{source->root, source->inputs};
    for (unsigned i = 0; i != 3; ++i) {
      const auto &row = source->rows[random() % source->rows.size()];
      tolerate_corruption(
          [&] { state.lookup(row.key, p::decode_key(row.key).kind); });
    }
    source->inputs[0].pages = nullptr;
  }
  tolerate_corruption([&] {
    auto expected = reference;
    const auto header = p::decode_page_header(page, p::Limits{});
    expected.digest = header.digest;
    expected.keys = header.keys;
    expected.height = header.height;
    x::PageValidator validator(source->account, source, page, expected);
    bounded(validator);
  });
  tolerate_corruption([&] {
    x::GraphValidator validator(source->account, source, source->inputs,
                                source->root, source->storage);
    bounded(validator);
  });
}
void fuzz_warm_sample(std::uint64_t &seed) {
  namespace x = l::detail;
  struct Base {
    std::shared_ptr<prepared_test::Fixture> source =
        std::make_shared<prepared_test::Fixture>(2, 500);
    l::RecoveredGeneration recovered = l::recover_generation(source->path);
    std::shared_ptr<d::StorageLedger> account =
        std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
  };
  static Base base;
  auto random = [&] {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
  };
  const auto original = base.source->segments[0].bytes.subspan(
      base.source->root.tree.offset, base.source->root.tree.length);
  auto bytes = std::make_shared<std::vector<char>>(original.begin(), original.end());
  for (unsigned i = 0, flips = random() % 5; i != flips; ++i)
    (*bytes)[random() % bytes->size()] ^= static_cast<char>(1 + random() % 255);
  try {
    auto root = base.source->root;
    ++root.cursor.revision;
    const auto digest = p::page_commitment(*bytes, p::Limits{});
    std::copy(digest.begin(), digest.end(), bytes->begin() + 32);
    l::SegmentOwnership ownership;
    ownership.storage = base.source->storage;
    ownership.segment.fill('z');
    ownership.length = bytes->size();
    ownership.checksum = p::pack_commitment(*bytes);
    root.tree.segment = ownership.segment;
    root.tree.offset = 0;
    root.tree.digest = digest;
    root.segments = 2;
    std::array<x::SegmentInput, 1> added{{{ownership, *bytes}}};
    x::WarmGraphValidator validator(base.account, base.recovered, bytes, added, root);
    bounded(validator);
  } catch (const std::invalid_argument &) {
  } catch (const x::ReadSourceFailure &) {
  }
}
double cpu_seconds() {
  rusage usage{};
  check(::getrusage(RUSAGE_SELF, &usage) == 0);
  return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
         (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
}
l::detail::PagedReadState read_state(const std::shared_ptr<Fixture> &source,
                                     const l::detail::VerifiedPages &verified) {
  source->inputs[0].pages = &verified;
  return {source->root, source->inputs};
}
void mutation(unsigned kind) {
  auto source = fixture(1000);
  l::detail::VerifiedPages verified(source->bytes.size(), source->account);
  auto state = read_state(source, verified);
  check(state.lookup(source->rows.front().key, p::PageKind::Definition)
            .has_value());
  const auto offset = source->root.tree.offset;
  auto root =
      std::span<char>(source->bytes).subspan(offset, source->root.tree.length);
  const auto node = p::decode_node(root, p::page_header_bytes, p::Limits{});
  if (kind == 0)
    l::write_integer(root, 12, 0x7fff, 2);
  else if (kind == 1)
    root[64] = 0;
  else if (kind == 2)
    l::write_integer(root, node.slots.data() - root.data(), 64, 2);
  else if (kind == 3)
    root[14] = char(0xff);
  else
    root[24] ^= 1;
  rejects<l::detail::ReadSourceFailure>(
      [&] { state.lookup(source->rows.front().key, p::PageKind::Definition); });
}
void mapped_mutation(unsigned variant) {
  auto source = fixture(1000);
  const auto path = std::filesystem::current_path() / ".scratch" /
                    ("dense-mapped-" + std::to_string(::getpid()) + "-" +
                     std::to_string(variant));
  std::filesystem::create_directories(path);
  const auto file =
      path / l::detail::SegmentNames(source->root.tree.segment).payload.data();
  const int payload =
      ::open(file.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
  check(payload >= 0);
  check(::write(payload, source->bytes.data(), source->bytes.size()) ==
        static_cast<ssize_t>(source->bytes.size()));
  const int directory =
      ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  check(directory >= 0);
  {
    d::RetirementRuntime runtime(source->account, 1);
    auto domain = runtime.domain(source->account);
    auto mappings = std::make_shared<d::StorageLedger>(2 * p::segment_bytes);
    auto access = domain.make<l::detail::LazyPageAccess>(
        source, directory, ::getuid(), source->storage, source->root,
        l::ViewLimits{}, domain.factory(), source->account, mappings);
    for (const auto &page : source->pages)
      access->resolve(page.reference);
    const auto verified = access->page_verifications();
    const auto root_offset = source->root.tree.offset;
    auto root_bytes = std::span<const char>(source->bytes)
                          .subspan(root_offset, source->root.tree.length);
    if (variant == 0) {
      auto redirected = source->pages.front().reference;
      redirected.offset = source->pages.back().reference.offset;
      redirected.length = source->pages.back().reference.length;
      rejects<l::detail::ReadSourceFailure>(
          [&] { access->resolve(redirected); });
    } else if (variant == 1) {
      auto interior = source->pages.front().reference;
      interior.offset += 8;
      rejects<l::detail::ReadSourceFailure>([&] { access->resolve(interior); });
    } else {
      const auto node = p::decode_node(root_bytes, 64, p::Limits{});
      std::size_t at = root_offset + 64;
      std::array<char, 2> replacement{};
      std::size_t size = 1;
      if (variant == 2) {
        at = root_offset + 12;
        replacement = {0x7f, char(0xff)};
        size = 2;
      } else if (variant == 3) {
        at = root_offset + (node.slots.data() - root_bytes.data());
        l::write_integer(replacement, 0, 64, 2);
        size = 2;
      }
      const auto mapped = access->resolve(source->root.tree);
      auto *destination = const_cast<char *>(mapped.data()) + (at - root_offset);
      const auto page_size = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
      const auto begin = reinterpret_cast<std::uintptr_t>(destination) & ~(page_size - 1);
      const auto end = (reinterpret_cast<std::uintptr_t>(destination + size) + page_size - 1) & ~(page_size - 1);
      check(::mprotect(reinterpret_cast<void *>(begin), end - begin, PROT_READ | PROT_WRITE) == 0);
      std::memcpy(destination, replacement.data(), size);
      check(::mprotect(reinterpret_cast<void *>(begin), end - begin, PROT_READ) == 0);
      using Handle = d::RetirementHandle<l::detail::LazyPageAccess>;
      auto held = std::make_shared<Handle>(access);
      l::detail::PagedReadState state{
          source->root,
          {},
          std::shared_ptr<const l::detail::PagedPageAccess>(held,
                                                            access.get())};
      try {
        rejects<l::detail::ReadSourceFailure>([&] {
          state.lookup(source->rows.front().key, p::PageKind::Definition);
        });
      } catch (const std::runtime_error &error) {
        throw std::runtime_error("mapped variant " + std::to_string(variant) + ": " + error.what());
      }
    }
    check(access->page_verifications() == verified);
    access.reset();
    domain.close().wait();
  }
  check(::close(directory) == 0 && ::close(payload) == 0);
  std::filesystem::remove_all(path);
}

} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && std::string_view(argv[1]) == "--fuzz") {
    const auto seconds = std::stoul(argv[2]);
    std::uint64_t seed = std::stoull(argv[3]);
    check(seed != 0);
    const auto begin = cpu_seconds();
    std::size_t samples = 0;
    do {
      fuzz_sample(seed);
      fuzz_warm_sample(seed);
      ++samples;
    } while (cpu_seconds() - begin < seconds);
    std::cout << samples << " bounded dense reader/page/graph/warm fuzz samples, "
              << cpu_seconds() - begin << " CPU seconds\n";
    return 0;
  }
  check(argc == 1);
  warm_skips();
  std::uint64_t seed = 0x83ac7f91;
  for (unsigned sample = 0; sample != 64; ++sample)
    fuzz_sample(seed);
  {
    auto account = std::make_shared<d::StorageLedger>(1024);
    l::detail::VerifiedPages verified(160, account);
    verified.set(0);
    check(verified.test(0) && !verified.test(8) && !verified.test(1));
    rejects<std::invalid_argument>([&] { verified.set(8); });
    verified.set(80);
    check(verified.test(80) && !verified.test(88) && !verified.test(10000));
  }
  for (auto count : {1u, 32u, 1000u}) {
    auto source = fixture(count);
    l::detail::VerifiedPages verified(source->bytes.size(), source->account);
    auto state = read_state(source, verified);
    const auto before = p::hash_counts().page_calls;
    for (const auto &row : source->rows) {
      const auto expected = p::decode_key(row.key).kind;
      const auto body = state.lookup(row.key, expected);
      check(body && std::equal(body->begin(), body->end(), row.body.begin(),
                               row.body.end()));
    }
    const auto warmed = p::hash_counts().page_calls;
    check(warmed == before + source->pages.size());
    for (const auto &row : source->rows)
      check(state.lookup(row.key, p::decode_key(row.key).kind).has_value());
    check(p::hash_counts().page_calls == warmed);
    check(!state.definition("ns", "absent"));
  }
  {
    auto source = fixture(1000);
    check(source->pages.size() > 2);
    l::detail::VerifiedPages verified(source->bytes.size(), source->account);
    source->inputs[0].pages = &verified;
    const auto &target = source->pages.front().reference;
    const auto &other = source->pages.back().reference;
    l::detail::checked_page_bytes(source->inputs[0], target);
    l::detail::checked_page_bytes(source->inputs[0], other);
    auto redirected = other;
    redirected.offset = target.offset;
    redirected.length = target.length;
    rejects<l::detail::ReadSourceFailure>(
        [&] { l::detail::checked_page_bytes(source->inputs[0], redirected); });
    redirected = target;
    redirected.offset += 8;
    rejects<std::exception>(
        [&] { l::detail::checked_page_bytes(source->inputs[0], redirected); });
  }
  {
    auto source = fixture(1);
    l::detail::VerifiedPages verified(source->bytes.size(), source->account);
    auto state = read_state(source, verified);
    const auto body =
        state.lookup(source->rows.front().key, p::PageKind::Definition);
    check(body && body->size() >= 24);
    const auto at = body->data() - source->bytes.data();
    // Value edits retaining valid framing remain visible until scrub.
    source->bytes[at + 23] ^= 1;
    const auto changed =
        state.lookup(source->rows.front().key, p::PageKind::Definition);
    check(changed && (*changed)[23] != source->rows.front().body[23]);
    auto bytes =
        std::span<char>(source->bytes)
            .subspan(source->root.tree.offset, source->root.tree.length);
    std::size_t node_at = p::page_header_bytes;
    for (;;) {
      const auto node = p::decode_node(bytes, node_at, p::Limits{});
      if (node.body.data() == body->data())
        break;
      node_at = node.end;
    }
    l::write_integer(bytes, node_at + 2, UINT16_MAX, 2);
    rejects<l::detail::ReadSourceFailure>([&] {
      state.lookup(source->rows.front().key, p::PageKind::Definition);
    });
  }
  for (unsigned kind = 0; kind < 5; ++kind)
    mutation(kind);
  for (unsigned variant = 0; variant < 5; ++variant)
    mapped_mutation(variant);
  std::cout << "dense reads, one-hash first touch, exact starts and mapped "
               "mutation bounds passed\n";
}
