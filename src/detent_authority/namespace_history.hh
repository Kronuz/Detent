#pragma once
// A voter's history of one namespace as the session endpoint answers from it (namespace-delivery
// D6, W6): the base, blocks and level-0 sections of the layout at the latest seal, the ring above
// it, and the state at that seal for a full copy. docs/status/namespace-s6-cutover.md.
#include "change_history.hh"
#include "detent_distribution/history.hh"
#include "history_layout.hh"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace detent::authority {
class AuthorityHistory final : public distribution::NamespaceHistory {
  public:
	// Freezes `name`'s history: `state` is the head, `sealed` the state at the latest seal and
	// `table` its layout, whose files `files` reads. Null when there is nothing to answer from:
	// positions not enabled, a layout not at `sealed`, or a namespace absent or dropped. The head
	// is the newest position the ring holds contiguously above the seal (a capture gap holds the
	// answer at the gap until the next seal covers it).
	static std::unique_ptr<AuthorityHistory> freeze(std::string_view name, const Identity &cluster,
													const State &state, const State &sealed,
													const history::Table &table, history::Files &files,
													const ChangeHistory &ring);
	distribution::NamespaceCursor head() const noexcept override { return head_; }
	std::uint64_t cut() const noexcept override { return cut_; }
	std::uint64_t ring_start() const noexcept override { return ring_start_; }
	std::span<const distribution::HistorySpan> spans() const noexcept override { return spans_; }
	std::unique_ptr<distribution::HistoryReader> read_span(std::size_t index) const override;
	std::unique_ptr<distribution::HistoryReader> read_ring(std::uint64_t after) const override;
	std::unique_ptr<distribution::HistoryReader> read_full_copy() const override;
	// Positions above the seal the ring lacks: the head waits for the next seal to cover them.
	std::uint64_t held_back() const noexcept { return held_back_; }

  private:
	AuthorityHistory(std::string name, const State &sealed, history::Files &files)
		: name_(std::move(name)), sealed_(sealed), files_(files) {}
	std::string name_;
	State sealed_;
	history::Files &files_;
	std::optional<history::TableNamespace> layout_;
	std::vector<const history::TableBlock *> tiers_;
	std::vector<distribution::HistorySpan> spans_;
	std::vector<std::shared_ptr<const CachedChange>> ring_;
	distribution::NamespaceCursor head_;
	std::uint64_t cut_ = 0, ring_start_ = 0, held_back_ = 0;
	Limits limits_;
};
// A ring entry's records: each changed key and its definition, or none for a delete, in key order.
std::vector<std::pair<std::string, std::optional<KnobDef>>> ring_records(const CachedChange &change,
																		 const Limits &limits);
} // namespace detent::authority
