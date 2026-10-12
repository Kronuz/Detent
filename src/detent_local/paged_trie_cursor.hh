#pragma once
#include "detent_local/paged_layout.hh"
#include "detent_local/view.hh"

namespace detent::local::detail {
// Reads canonical typed keys from immutable admitted input in ascending order.
using TrieKeyReader = std::size_t (*)(const void *, std::uint64_t, std::span<char>);
// Readiness is idempotent and retains current/next rows until later ordinals.
using TrieSourceReady = bool (*)(const void *, std::uint64_t, ValidationBudget &);
struct TrieEvent {
	paged::PageKind kind;
	std::uint64_t ordinal = 0, keys = 0;
	std::string_view key, prefix;
	std::span<const paged::Child> children;
};
class TrieCursor {
  public:
	TrieCursor(std::shared_ptr<distribution::StorageLedger>, std::shared_ptr<const void> source,
			   TrieKeyReader, std::uint64_t keys, paged::Limits = {}, TrieSourceReady = nullptr);
	~TrieCursor();
	TrieCursor(TrieCursor &&) noexcept;
	TrieCursor(const TrieCursor &) = delete;
	void step(ValidationBudget &);
	bool ready() const noexcept;
	TrieEvent event() const;
	// Event spans remain stable until the emitted node is committed.
	bool commit(const paged::Reference &, ValidationBudget &);
	bool done() const noexcept;
	paged::Reference root() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
