#pragma once
#include "encoded_update.hh"
#include <array>
#include <optional>

namespace detent::distribution {
struct CacheMarker {
	Identity incarnation{};
	std::uint64_t lineage = 0;
	bool operator==(const CacheMarker &) const = default;
};
enum class CacheDecision { Update, Idle, SnapshotRequired, SourceBehind, Busy, IdentityChanged, Fenced };
struct CacheOffer {
	CacheDecision decision = CacheDecision::Busy;
	RetirementHandle<EncodedUpdate> update;
	CacheMarker marker;
};
struct SnapshotPath {
	CacheDecision decision = CacheDecision::Busy;
	Cursor head{};
	CacheMarker marker{};
	std::size_t count = 0;
};
// One configured exact bundle. Hosts verify durable recovery before selection;
// cache pressure records a gap, and transport originals retain their own bytes.
class UpdateCache {
  public:
	static constexpr std::size_t maximum_edges = 256;
	UpdateCache(Identity incarnation, std::size_t edges, bool retain_snapshot = false);
	void select_publication(Cursor, RetirementHandle<EncodedUpdate> = {});
	void install_snapshot(RetirementHandle<EncodedUpdate>);
	CacheOffer offer(std::optional<Cursor> base, bool force_snapshot = false) const;
	bool covers(CacheMarker, const Cursor &) const noexcept;
	SnapshotPath capture_snapshot_path(std::optional<Cursor>,
		std::span<RetirementHandle<EncodedUpdate>>) const;
	void fence() noexcept;
	bool release_stale(WorkBudget &) noexcept;
	const std::optional<Cursor> &selected() const noexcept { return head_; }
	CacheMarker marker() const noexcept { return {incarnation_, lineage_}; }
	std::size_t retained_edges() const noexcept;
	std::optional<Cursor> snapshot_frontier() const noexcept;
	bool covers_to_head(const Cursor &) const noexcept;
	std::optional<std::size_t> edges_to_head(const Cursor &) const noexcept;

  private:
	struct Edge {
		RetirementHandle<EncodedUpdate> update;
		std::uint64_t lineage = 0;
	};
	std::optional<std::size_t> walk_to_head(const Cursor &,
		std::span<RetirementHandle<EncodedUpdate>>) const noexcept;
	void gap();
	Identity incarnation_;
	const std::size_t limit_;
	const bool retain_snapshot_;
	std::array<Edge, maximum_edges> edges_;
	RetirementHandle<EncodedUpdate> snapshot_;
	std::optional<Cursor> head_;
	std::uint64_t lineage_ = 1;
	std::size_t next_ = 0, cleanup_ = 0, credit_ = 0;
	bool fenced_ = false;
};
} // namespace detent::distribution
