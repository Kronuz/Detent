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
// One configured exact bundle. Hosts verify durable recovery before selection;
// cache pressure records a gap, and transport originals retain their own bytes.
class UpdateCache {
  public:
	static constexpr std::size_t maximum_edges = 256;
	UpdateCache(Identity incarnation, std::size_t edges);
	void select_publication(Cursor, RetirementHandle<EncodedUpdate> = {});
	void install_snapshot(RetirementHandle<EncodedUpdate>);
	CacheOffer offer(std::optional<Cursor> base, bool force_snapshot = false) const;
	bool covers(CacheMarker, const Cursor &) const noexcept;
	void fence() noexcept;
	bool release_stale(WorkBudget &) noexcept;
	const std::optional<Cursor> &selected() const noexcept { return head_; }
	CacheMarker marker() const noexcept { return {incarnation_, lineage_}; }
	std::size_t retained_edges() const noexcept;

  private:
	struct Edge {
		RetirementHandle<EncodedUpdate> update;
		std::uint64_t lineage = 0;
	};
	void gap();
	Identity incarnation_;
	const std::size_t limit_;
	std::array<Edge, maximum_edges> edges_;
	RetirementHandle<EncodedUpdate> snapshot_;
	std::optional<Cursor> head_;
	std::uint64_t lineage_ = 1;
	std::size_t next_ = 0, cleanup_ = 0, credit_ = 0;
	bool fenced_ = false;
};
} // namespace detent::distribution
