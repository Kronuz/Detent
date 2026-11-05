#pragma once
#include "detent_distribution/buffer.hh"
#include "paged_trie_cursor.hh"
#include <stop_token>

namespace detent::local::detail {
struct SealedPage {
	std::shared_ptr<const distribution::OwnedBuffer> bytes;
	paged::Reference summary;
	std::string_view path;
};
// Consumes postorder events and emits pages in canonical seal order.
class PagedPacker {
  public:
	explicit PagedPacker(std::shared_ptr<distribution::StorageLedger>, paged::Limits = {},
						 std::stop_token = {});
	~PagedPacker();
	PagedPacker(PagedPacker &&) noexcept;
	PagedPacker(const PagedPacker &) = delete;
	// Borrowed event/body spans stay immutable until accepted() becomes true.
	void push(TrieEvent, std::span<const char> body = {});
	void finish();
	void step(ValidationBudget &);
	bool accepted() const noexcept;
	bool ready() const noexcept;
	SealedPage page() const;
	// Binds the emitted page to its admitted segment extent before continuing.
	bool commit(const paged::Reference &, ValidationBudget &);
	bool done() const noexcept;
	paged::Reference root() const;
	bool discard(ValidationBudget &);

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
} // namespace detent::local::detail
