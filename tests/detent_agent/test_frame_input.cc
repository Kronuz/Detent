// W6 frames into the paged builders (FrameInput): an agent at R applies each answer the authority
// gives (the ring's entries, the block trimmed above R then entries, a full copy then entries) and
// reaches the state commitment of the full copy at h. A tombstone for a key the store lacks is a
// no-op; a tombstone for a key it holds deletes it.
#include "../detent_local/paged_disk_fixture.hh"
#include "detent_distribution/frame_input.hh"
#include "paged_delta_plan.hh"
#include "paged_plan.hh"
#include <map>
#include <optional>

namespace {
using Value = std::optional<std::int64_t>; // nullopt: a tombstone
struct Change {
	std::uint64_t revision;
	std::string key;
	Value value;
};
struct Piece {
	d::PieceKind kind;
	std::uint64_t first, last;
	std::vector<Change> records; // key order
};
const d::Identity cluster{'c'}, incarnation{'i'};
std::shared_ptr<d::StorageLedger> account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);

d::FrameInput frame(d::FrameKind kind, std::uint64_t after, std::uint64_t to, const std::vector<Piece> &pieces) {
	d::Limits limits;
	limits.storage_account = account;
	d::FrameInput input("app", kind, after, {cluster, incarnation, to}, {}, limits);
	for (const auto &piece : pieces) {
		input.piece(piece.kind, piece.first, piece.last);
		for (const auto &change : piece.records) {
			std::optional<detent::KnobDef> value;
			if (change.value)
				value.emplace().default_value = *change.value;
			input.record(change.key, change.revision, value ? &*value : nullptr);
		}
	}
	return input;
}
std::vector<Piece> entries(std::uint64_t after, std::uint64_t to, const std::vector<Change> &ring) {
	std::vector<Piece> out;
	for (auto p = after + 1; p <= to; ++p) {
		Piece piece{d::PieceKind::Entry, p, p, {}};
		for (const auto &change : ring)
			if (change.revision == p)
				piece.records.push_back(change);
		std::sort(piece.records.begin(), piece.records.end(), [](auto &a, auto &b) { return a.key < b.key; });
		out.push_back(std::move(piece));
	}
	return out;
}
template <class Builder> auto drive(Builder &builder) {
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	return builder.take();
}
l::paged::Digest commitment(std::shared_ptr<const d::ValidatedSnapshot> snapshot, char generation) {
	l::GenerationId id{};
	id.fill(generation);
	l::detail::PagedSnapshotBuilder builder(std::move(snapshot), id);
	return l::paged::state_commitment(drive(builder).root());
}

// The authority's history after R = 10, up to h = 14: app:e is created and deleted after R.
constexpr std::uint64_t R = 10, h = 14;
const std::vector<Change> ring{
	{11, "app:b", 20}, {11, "app:d", 4},  {12, "app:a", {}}, {12, "app:e", 5},
	{13, "app:c", 30}, {13, "app:e", {}}, {14, "app:b", 21},
};
} // namespace

int main() {
	const auto directory = std::filesystem::current_path() / ("frame-input-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	try {
		const std::vector<Change> at_r{{3, "app:a", 1}, {5, "app:b", 2}, {7, "app:c", 3}};
		const std::vector<Change> at_h{{14, "app:b", 21}, {13, "app:c", 30}, {11, "app:d", 4}};
		const auto expected =
			commitment(frame(d::FrameKind::FullCopy, 0, h, {{d::PieceKind::FullCopy, 0, h, at_h}}).snapshot(), 'f');

		// The store at R, built from a full copy frame.
		l::GenerationId base_id{};
		base_id.fill('r');
		l::detail::PagedSnapshotBuilder base_builder(
			frame(d::FrameKind::FullCopy, 0, R, {{d::PieceKind::FullCopy, 0, R, at_r}}).snapshot(), base_id);
		const auto base_image = drive(base_builder);
		install(directory, base_image);
		auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 64);
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		std::optional<l::RecoveredGeneration> base(l::recover_generation(directory, limits));
		auto apply = [&](d::FrameInput input) {
			l::detail::PagedDeltaPlanBuilder builder(*base, input.delta(), domain.factory(), account);
			return l::paged::state_commitment(drive(builder).root());
		};

		// Entries: one piece per position after R.
		require(apply(frame(d::FrameKind::Entries, R, h, entries(R, h, ring))) == expected);
		// Blocks: the block holding R + 1, trimmed above R (each key's last change), then entries.
		auto blocks = entries(13, h, ring);
		blocks.insert(blocks.begin(), Piece{d::PieceKind::Span, R + 1, 13,
											{{12, "app:a", {}}, {13, "app:c", 30}, {11, "app:d", 4},
											 {13, "app:e", {}}}});
		require(apply(frame(d::FrameKind::Blocks, R, h, blocks)) == expected);
		// A full copy at s = 12, then the ring's entries after it.
		auto copy = entries(12, h, ring);
		copy.insert(copy.begin(), Piece{d::PieceKind::FullCopy, 0, 12,
										{{11, "app:b", 20}, {7, "app:c", 3}, {11, "app:d", 4}, {12, "app:e", 5}}});
		require(commitment(frame(d::FrameKind::FullCopy, 0, h, copy).snapshot(), 'g') == expected);
		require(expected != l::paged::state_commitment(base_image.root()));

		// A tombstone for a key the store lacks moves only the position; for a key it holds, deletes.
		const auto absent = apply(frame(d::FrameKind::Entries, R, 11, {{d::PieceKind::Entry, 11, 11, {{11, "app:z", {}}}}}));
		const std::vector<Change> at_11{{3, "app:a", 1}, {5, "app:b", 2}, {7, "app:c", 3}};
		require(absent ==
				commitment(frame(d::FrameKind::FullCopy, 0, 11, {{d::PieceKind::FullCopy, 0, 11, at_11}}).snapshot(), 'v'));
		const auto deleted =
			apply(frame(d::FrameKind::Entries, R, 11, {{d::PieceKind::Entry, 11, 11, {{11, "app:a", {}}}}}));
		const std::vector<Change> deleted_11{{5, "app:b", 2}, {7, "app:c", 3}};
		require(deleted ==
				commitment(frame(d::FrameKind::FullCopy, 0, 11, {{d::PieceKind::FullCopy, 0, 11, deleted_11}}).snapshot(),
						   't'));

		// A record outside its frame is refused.
		bool refused = false;
		try {
			frame(d::FrameKind::Entries, R, 11, {{d::PieceKind::Entry, 11, 11, {{12, "app:a", 1}}}});
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		require(refused);
		base.reset();
		domain.close().wait();
		runtime.shutdown();
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_frame_input: %s\n", error.what());
		return 1;
	}
	std::printf("frame input: entries, blocks and a full copy reach the commitment at h\n");
	return 0;
}
