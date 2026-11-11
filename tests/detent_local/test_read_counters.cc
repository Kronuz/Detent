#include "detent_local/read_counters.hh"
#include "prepared_reader_fixture.hh"
#include <iostream>

namespace c = detent::local::counters;
using prepared_test::check;
using prepared_test::Fixture;
namespace {
void expect(const c::Snapshot &got, const c::Snapshot &want, int line) {
	if (got == want)
		return;
	std::cerr << "line " << line << ": got " << got.directory_trust << ' ' << got.record_opens << ' '
	          << got.table_bytes << ' ' << got.segment_opens << ' ' << got.pages_verified << ' '
	          << got.bytes_hashed << ", want " << want.directory_trust << ' ' << want.record_opens << ' '
	          << want.table_bytes << ' ' << want.segment_opens << ' ' << want.pages_verified << ' '
	          << want.bytes_hashed << '\n';
	throw std::runtime_error("counter mismatch");
}
#define EXPECT(got, ...) expect(got, c::Snapshot __VA_ARGS__, __LINE__)
} // namespace
int main() {
	namespace l = detent::local;
	namespace x = l::detail;
	auto fixture = std::make_shared<Fixture>();
	c::reset();
	check(c::snapshot() == c::Snapshot{});
	auto prepared = x::PreparedReader::open(fixture->path, ::getuid(), {});
	const auto opened = c::snapshot();
	if (!c::enabled) {
		// Compiled out: nothing ever counts.
		EXPECT(opened, ({}));
		check(prepared.view.get("a", "k").value() == 42);
		EXPECT(c::snapshot(), ({}));
		return 0;
	}
	// selected, lease, storage.meta, image, reader metadata: five records, one directory walk.
	const std::uint64_t table = l::descriptor_bytes + l::generation_ownership_bytes + l::storage_identity_bytes +
	                            l::paged::root_bytes + x::reader_metadata_bytes;
	const std::uint64_t hashed = (l::generation_ownership_bytes - 32) + (l::storage_identity_bytes - 32) +
	                             l::paged::root_bytes + 160;
	EXPECT(opened, ({1, 5, table, 0, 0, hashed}));
	// Dense packs this four-row tree into one root page in one pack: the first lookup opens that
	// pack and verifies that page, hashing all of it but its 32-byte digest field.
	const auto tree = fixture->root.tree;
	const auto &pack = *fixture->buffers[0];
	const auto header = l::paged::decode_page_header(
		std::span<const char>(pack.data(), pack.size()).subspan(tree.offset, tree.length), l::paged::Limits{});
	check(fixture->segments.size() == 1 && header.externals == 0);
	check(prepared.view.get("a", "k").value() == 42);
	const auto first = c::snapshot() - opened;
	EXPECT(first, ({0, 0, 0, 1, 1, tree.length - 32}));
	// Warm repeat: nothing opens, verifies or hashes.
	const auto warm = c::snapshot();
	for (int i = 0; i != 5; ++i)
		check(prepared.view.get("a", "k").value() == 42);
	EXPECT(c::snapshot() - warm, ({}));
	// The other namespace's knob lives in the same verified page: nothing more opens or hashes.
	check(prepared.view.eval("b", "k").value() == true);
	EXPECT(c::snapshot() - warm, ({}));
	// Another open walks the directory and reads the records again.
	const auto before = c::snapshot();
	auto again = x::PreparedReader::open(fixture->path, ::getuid(), {});
	EXPECT(c::snapshot() - before, ({1, 5, table, 0, 0, hashed}));
	return 0;
}
