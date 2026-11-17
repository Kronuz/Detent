// A delta's warm validation reads only the definitions it changes. A definition the delta moves unchanged to a
// new page meets its old leaf and neither is read, so at f 0.05 the leaf work is twice the changed keys at every
// store size; a moved definition whose bytes differ is validated, and rejected when invalid.
//   test_delta_plan_moved_definitions [SIZE ...]   only the probe, at these store sizes
#include "detent_local/paged_definition.hh"
#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
#include "paged_limits.hh"
#include "paged_overlay_assembler.hh"
#include "paged_warm_validator.hh"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <source_location>
#include <unistd.h>
namespace p = l::paged;
namespace x = l::detail;
namespace {
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("moved definitions invariant at line " + std::to_string(location.line()));
}
std::vector<x::SegmentInput> inputs(const x::PagedImage &image) {
	std::vector<x::SegmentInput> segments;
	for (const auto &segment : image.segments())
		segments.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
	std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
	});
	return segments;
}
// Format-wide bounds, so the probe builds stores past the default 500K-record limits.
l::ViewLimits view_limits() {
	auto limits = l::ViewLimits::prepared();
	limits.mapping_account = std::make_shared<d::StorageLedger>(64ull << 30);
	return limits;
}
const p::Limits paged_limits = x::paged_limits(l::ViewLimits::prepared());
d::ValidatedSnapshot decode_wide(std::string_view bytes) {
	d::Limits limits;
	limits.bytes = 4ull << 30;
	limits.string_bytes = 2ull << 30;
	limits.records = 8'000'000;
	limits.storage_bytes = 12ull << 30;
	d::SnapshotDecoder decoder(cursor(), {"app"}, limits);
	auto input = std::span(bytes.data(), bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget{1 << 20, 1 << 12};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
// A store of `count` definitions and the delta that gives every `stride`-th one a new default.
struct Delta {
	std::filesystem::path directory;
	std::shared_ptr<d::StorageLedger> account;
	std::optional<l::RecoveredGeneration> base;
	std::shared_ptr<x::PagedImage> result;
	std::vector<x::SegmentInput> added;
	std::uint64_t changed = 0;
	Delta(unsigned count, unsigned stride) {
		directory = std::filesystem::current_path() / ".scratch" /
					("moved-definitions-" + std::to_string(::getpid()) + "-" + std::to_string(count));
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode_wide(many(count)));
		account = source->storage_account();
		l::GenerationId generation{}, next{};
		generation.fill('g');
		next.fill('n');
		x::PagedSnapshotBuilder original(source, generation, paged_limits);
		while (!original.done()) {
			l::ValidationBudget work;
			original.step(work);
		}
		auto image = std::make_shared<x::PagedImage>(original.take());
		const auto segments = inputs(*image);
		std::vector<x::SegmentReachability> live;
		{
			x::GraphValidator graph(account, image, segments, image->root(), paged_limits,
									x::paged_format_semantics);
			while (!graph.done()) {
				l::ValidationBudget work;
				graph.step(work);
			}
			for (std::size_t i = 0; i < segments.size(); ++i)
				live.push_back(graph.segment_reachability(i));
		}
		install(directory, *image, l::ViewLimits::prepared());
		base.emplace(l::recover_generation(directory, view_limits()));
		x::TrieOverlay overlay(account, image, segments, image->root(), paged_limits);
		for (unsigned i = 0; i < count; i += stride) {
			char name[16];
			std::snprintf(name, sizeof name, "app:k%06u", i);
			std::array<char, 202> key{};
			const auto length = p::encode_definition_key(key, name);
			p::Definition definition;
			definition.revision = 2;
			definition.default_value = -1 - std::int64_t(i);
			std::vector<char> body(p::header_size(definition));
			p::encode_definition_header(body, definition);
			overlay.change({key.data(), length}, std::span<const char>(body));
			while (!overlay.operation_done()) {
				l::ValidationBudget work;
				overlay.step(work);
			}
			check(overlay.changed());
			++changed;
		}
		overlay.seal();
		auto target = cursor();
		target.revision = 43;
		x::OverlayAssembler assembler(account, overlay, segments, live, image->root(), next, target,
									 paged_limits);
		while (!assembler.done()) {
			l::ValidationBudget work;
			assembler.step(work);
		}
		result = std::make_shared<x::PagedImage>(assembler.take());
		added = inputs(*result);
		for (;;) {
			l::ValidationBudget work;
			if (assembler.discard(work))
				break;
		}
		for (;;) {
			l::ValidationBudget work;
			if (overlay.discard(work))
				break;
		}
	}
	~Delta() {
		base.reset();
		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}
};
struct Leaves {
	std::uint64_t validated = 0, removed = 0, moved = 0;
};
// Validates `root` over the base with `added` as the new segments; throws on a rejected graph.
Leaves validate(const Delta &delta, std::span<const x::SegmentInput> added, p::Root root) {
	x::WarmGraphValidator warm(delta.account, *delta.base, delta.result, added, root, paged_limits, x::paged_format_semantics,
							   true);
	for (unsigned turn = 0; !warm.done(); ++turn) {
		check(turn < 100000000);
		l::ValidationBudget work{1, 1};
		warm.step(work);
	}
	const Leaves leaves{warm.definitions_validated(), warm.old_definition_headers(), warm.moved_definitions()};
	for (;;) {
		l::ValidationBudget work;
		if (warm.discard(work))
			break;
	}
	return leaves;
}
// The leaf work of a uniform f 0.05 delta at `count` definitions: twice the changed keys, nothing per moved one.
void probe(unsigned count) {
	const Delta delta(count, 20);
	const auto leaves = validate(delta, delta.added, delta.result->root());
	std::cout << count << " definitions, " << delta.changed << " changed, " << delta.result->root().pages
			  << " pages written: " << leaves.validated << " validated, " << leaves.removed << " old read, "
			  << leaves.moved << " moved unread; leaf reads per changed key "
			  << double(leaves.validated + leaves.removed) / double(delta.changed) << "\n";
	check(leaves.validated == delta.changed && leaves.removed == delta.changed);
	check(leaves.moved >= 8 * delta.changed);
}
// Rewrites the first definition of the single new page whose revision is `from` to revision `to`, then
// seals the page and its pack again, so only the definition's own validation can reject it.
void rewrite(std::vector<char> &segment, p::Root &root, x::SegmentInput &input, std::uint64_t from,
			 std::uint64_t to) {
	check(root.tree.segment == input.ownership.segment);
	std::span<char> page(segment.data() + root.tree.offset, root.tree.length);
	const auto header = p::decode_page_header(page, p::Limits{});
	const auto end = page.size() - p::child_bytes * header.externals;
	bool found = false;
	for (std::size_t offset = p::page_header_bytes; offset < end && !found;) {
		const auto node = p::decode_node(page, offset, p::Limits{});
		if (node.kind == p::PageKind::Definition && l::read_integer(node.body, 0, 8) == from) {
			l::write_integer(page, std::size_t(node.body.data() - page.data()), to, 8);
			found = true;
		}
		offset = node.end;
	}
	check(found);
	root.tree.digest = p::page_commitment(page, p::Limits{});
	std::copy(root.tree.digest.begin(), root.tree.digest.end(), page.begin() + 32);
	input.ownership.checksum = p::pack_commitment(segment);
	input.bytes = {segment.data(), segment.size()};
}
// A one-page store: the delta rewrites its only page, moving every other definition.
void corrupt_moved_definition() {
	const Delta delta(60, 1000);
	check(delta.result->root().pages == 1 && delta.added.size() == 1);
	const auto clean = validate(delta, delta.added, delta.result->root());
	check(clean.validated == 1 && clean.removed == 1 && clean.moved == 59);
	// A moved definition whose revision passes its namespace's: same key, different bytes, invalid.
	std::vector<char> bytes(delta.added[0].bytes.begin(), delta.added[0].bytes.end());
	auto root = delta.result->root();
	auto added = delta.added;
	rewrite(bytes, root, added[0], 1, 99);
	bool rejected = false;
	try {
		validate(delta, added, root);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	check(rejected);
	// The same rewrite to a revision its namespace allows is a valid state: validated, then accepted.
	bytes.assign(delta.added[0].bytes.begin(), delta.added[0].bytes.end());
	root = delta.result->root();
	added = delta.added;
	rewrite(bytes, root, added[0], 1, 3);
	const auto valid = validate(delta, added, root);
	check(valid.validated == 2 && valid.removed == 2 && valid.moved == 58);
}
} // namespace
int main(int argc, char **argv) {
	if (argc > 1) {
		for (int i = 1; i < argc; ++i)
			probe(unsigned(std::stoul(argv[i])));
		return 0;
	}
	corrupt_moved_definition();
	for (unsigned count : {2000u, 32000u})
		probe(count);
	std::cout << "moved definitions are matched, changed and corrupt ones validated\n";
}
