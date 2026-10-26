#pragma once
#include "detent_local/paged_layout.hh"
#include <stdexcept>

namespace detent::local::detail {
class ReadSourceFailure : public std::runtime_error {
  public:
	explicit ReadSourceFailure(const char *message) : std::runtime_error(message) {}
};
class PagedPageAccess {
  public:
	virtual ~PagedPageAccess() = default;
	// Returned bytes are checked and remain mapped for this root's lifetime.
	virtual std::span<const char> resolve(const paged::Reference &) const = 0;
	virtual std::uint32_t maximum_overrides() const noexcept = 0;
	virtual bool available() const noexcept = 0;
};
} // namespace detent::local::detail
