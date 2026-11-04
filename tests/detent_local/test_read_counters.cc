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
	// First lookup of a:k opens the index segment and a's segment and verifies five pages: the root,
	// the definitions branch, the definition, the namespaces branch and a's namespace page. Hashed by
	// hand: physical 272+272+272+107+89 = 1012, logical 110+110+110+71+53 = 454.
	check(prepared.view.get("a", "k").value() == 42);
	const auto first = c::snapshot() - opened;
	EXPECT(first, ({0, 0, 0, 2, 5, 1466}));
	// Warm repeat: nothing opens, verifies or hashes.
	const auto warm = c::snapshot();
	for (int i = 0; i != 5; ++i)
		check(prepared.view.get("a", "k").value() == 42);
	EXPECT(c::snapshot() - warm, ({}));
	// A second knob in the other namespace opens one more segment and verifies the pages it needs.
	check(prepared.view.eval("b", "k").value() == true);
	const auto second = c::snapshot() - warm;
	// Hashed by hand: b's definition 116 + 80 and its namespace page 89 + 53.
	EXPECT(second, ({0, 0, 0, 1, 2, 338}));
	// Another open walks the directory and reads the records again.
	const auto before = c::snapshot();
	auto again = x::PreparedReader::open(fixture->path, ::getuid(), {});
	EXPECT(c::snapshot() - before, ({1, 5, table, 0, 0, hashed}));
	return 0;
}
