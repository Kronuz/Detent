#pragma once
#include "paged_graph_validator.hh"
namespace detent::local::detail {
struct OverlayNode;
struct OverlayReference {
	const char *old = nullptr;
	OverlayNode *fresh = nullptr;
	explicit operator bool() const noexcept { return old || fresh; }
};
struct OverlayChild {
	std::uint8_t label = 0;
	OverlayReference reference;
};
struct OverlayNode {
	paged::PageKind kind = paged::PageKind::Definition;
	std::uint64_t keys = 0;
	std::string_view prefix;
	std::span<const char> body;
	std::span<OverlayChild> children;
	std::size_t capacity = 0;
};
// Worker-owned structural recipes borrow checked mappings; they grant no IO authority.
class TrieOverlay {
  public:
	TrieOverlay(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> immutable_owner,
				std::span<const SegmentInput>, paged::Root, paged::Limits = {});
	~TrieOverlay();
	TrieOverlay(TrieOverlay &&) noexcept;
	TrieOverlay(const TrieOverlay &) = delete;
	// The optional body stays borrowed until this operation completes or is discarded.
	void change(std::string_view typed_key, std::optional<std::span<const char>> body);
	void begin_relocation(std::span<const StorageIdentity>, std::size_t maximum_recipes);
	void step(ValidationBudget &);
	std::uint64_t visited_pages() const;
	std::size_t relocated_pages() const;
	bool operation_done() const noexcept;
	bool changed() const;
	std::optional<std::span<const char>> previous_body() const;
	void seal();
	OverlayReference root() const;
	std::span<const SegmentReachability> removed() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
