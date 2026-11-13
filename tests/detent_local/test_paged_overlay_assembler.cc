#include "detent_local/paged_definition.hh"
#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
#include "paged_overlay_assembler.hh"
#include "paged_overlay_fixture.hh"
#include "paged_warm_validator.hh"
#include <algorithm>
#include <cstring>
namespace l = detent::local;
#include <source_location>
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("delta fixture invariant at " + std::to_string(location.line()));
}
int main() {
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(many(20000)));
	auto account = source->storage_account();
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder original(source, generation);
	while (!original.done()) {
		l::ValidationBudget work;
		original.step(work);
	}
	auto image = std::make_shared<l::detail::PagedImage>(original.take());
	check(!image->segments().empty());
	std::vector<l::detail::SegmentInput> segments;
	for (const auto &segment : image->segments())
		segments.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
	std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
	});
	std::vector<l::detail::SegmentReachability> live;
	{
		l::detail::GraphValidator graph(account, image, segments, image->root(), {}, l::detail::paged_format_semantics);
		while (!graph.done()) {
			l::ValidationBudget work;
			graph.step(work);
		}
		for (std::size_t i = 0; i < segments.size(); ++i)
			live.push_back(graph.segment_reachability(i));
	}
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("warm-assembly-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path directory;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(directory, error);
		}
	} cleanup{directory};
	install(directory, *image, storage);
	auto recovered = l::recover_generation(directory);
	l::detail::TrieOverlay overlay(account, image, segments, image->root());
	std::array<char, 202> key{};
	std::array<char, 8> watermark{};
	l::write_integer(watermark, 0, 43, 8);
	auto length = p::encode_namespace_key(key, "app");
	overlay.change({key.data(), length}, watermark);
	while (!overlay.operation_done()) {
		l::ValidationBudget work{1, 1};
		overlay.step(work);
	}
	p::Definition definition;
	definition.revision = 43;
	definition.default_value = -9;
	std::array<char, p::definition_header_bytes> body{};
	p::encode_definition_header(body, definition);
	length = p::encode_definition_key(key, "app:k000000");
	overlay.change({key.data(), length}, body);
	while (!overlay.operation_done()) {
		l::ValidationBudget work{1, 1};
		overlay.step(work);
	}
	overlay.seal();
	auto target = cursor();
	target.revision = 43;
	const auto baseline = account->used();
	{
		l::detail::OverlayAssembler builder(account, overlay, segments, live, image->root(), next, target);
		for (unsigned turn = 0; !builder.done(); ++turn) {
			check(turn < 2000000);
			l::ValidationBudget work{1, 1};
			builder.step(work);
		}
		auto result = std::make_shared<l::detail::PagedImage>(builder.take());
		check(result->segments().size() == 1 && result->segments()[0].ownership.length < 65536);
		check(result->root().segments == image->root().segments + 1 &&
				result->root().pages < image->root().pages + 32);
		std::vector<l::detail::SegmentInput> merged = segments;
		merged.push_back(
			{result->segments()[0].ownership,
			 {result->segments()[0].bytes->data(), result->segments()[0].bytes->size()}});
		std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
			return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), 16) < 0;
		});
		{
			l::detail::GraphValidator graph(account, result, merged, result->root(), {}, l::detail::paged_format_semantics);
			while (!graph.done()) {
				l::ValidationBudget work;
				graph.step(work);
			}
			check(graph.counts().records == 20000);
		}
		{
			std::vector<l::detail::SegmentInput> added{
				{result->segments()[0].ownership,
				 {result->segments()[0].bytes->data(), result->segments()[0].bytes->size()}}};
			l::detail::WarmGraphValidator warm(account, recovered, result, added, result->root(), {}, l::detail::paged_format_semantics);
			for (unsigned turn = 0; !warm.done(); ++turn) {
				check(turn < 2000000);
				l::ValidationBudget work{1, 1};
				warm.step(work);
			}
			check(warm.counts().records == 20000 && warm.counts().strings == 220003 &&
					warm.counts().maximum_overrides == 0);
			check(warm.hashed_segment_bytes() == result->segments()[0].bytes->size());
			for (std::size_t i = 0; i < segments.size(); ++i)
				check(warm.removed(i).pages == builder.removed()[i].pages &&
						warm.removed(i).bytes == builder.removed()[i].bytes);
			for (unsigned turn = 0;; ++turn) {
				check(turn < 2000000);
				l::ValidationBudget work{1, 1};
				if (warm.discard(work))
					break;
			}
		}
		auto expected =
			std::make_shared<const d::ValidatedSnapshot>(decode(many(20000, 43, true), target));
		l::detail::PagedSnapshotBuilder oracle(expected, next);
		while (!oracle.done()) {
			l::ValidationBudget work;
			oracle.step(work);
		}
		auto wanted = oracle.take();
		check(p::state_commitment(result->root()) == p::state_commitment(wanted.root()) &&
				result->root().bytes == wanted.root().bytes &&
				result->root().pages == wanted.root().pages);
		for (unsigned turn = 0;; ++turn) {
			check(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (builder.discard(work))
				break;
		}
	}
	check(account->used() == baseline);
	{
		l::detail::OverlayAssembler builder(account, overlay, segments, live, image->root(), next, target);
		for (unsigned turn = 0; account->used() - baseline < p::segment_bytes; ++turn) {
			check(turn < 2000000 && !builder.done());
			l::ValidationBudget work{1, 1};
			builder.step(work);
		}
		l::ValidationBudget none{0, 0};
		check(!builder.discard(none));
		for (unsigned turn = 0;; ++turn) {
			check(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (builder.discard(work))
				break;
		}
	}
	check(account->used() == baseline);
	{
		auto incorrect = live;
		++incorrect[0].pages;
		l::detail::OverlayAssembler builder(account, overlay, segments, incorrect, image->root(), next, target);
		bool rejected = false;
		try {
			while (!builder.done()) {
				l::ValidationBudget work;
				builder.step(work);
			}
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		check(rejected);
		for (unsigned turn = 0;; ++turn) {
			check(turn < 2000000);
			l::ValidationBudget work{1, 1};
			if (builder.discard(work))
				break;
		}
	}
	check(account->used() == baseline);
	std::cout << "20k shared definitions, sparse bytes, cold validation and partial-pack "
				 "cancellation passed\n";
}
