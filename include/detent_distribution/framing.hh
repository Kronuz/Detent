#pragma once
#include "buffer.hh"
#include "protocol.hh"
#include <optional>

namespace detent::distribution {
inline constexpr std::size_t frame_header_bytes = 96, fragment_bytes = 65536;
struct Fragment {
	UpdateKind kind = UpdateKind::Snapshot;
	Identity update{};
	std::uint64_t offset = 0, total = 0;
	bool final = false;
	std::string bytes;
};
std::string encode_fragment(const Fragment &fragment, bool compress = false);
// Received bytes retain their physical admission independently of the decoder.
struct DecodedFragment {
	UpdateKind kind = UpdateKind::Snapshot;
	Identity update{};
	std::uint64_t offset = 0, total = 0;
	bool final = false;
	std::shared_ptr<const OwnedBuffer> body;
	std::string_view bytes() const noexcept { return body ? body->view() : std::string_view{}; }
};
class FragmentDecoder {
  public:
	FragmentDecoder(UpdateKind expected_kind, std::uint64_t maximum_bytes, bool allow_compression = false,
					std::shared_ptr<StorageLedger> storage_account = {});
	~FragmentDecoder();
	FragmentDecoder(FragmentDecoder &&) noexcept;
	FragmentDecoder(const FragmentDecoder &) = delete;
	// One output slot. Stops consumption while a complete fragment awaits take().
	void feed(std::span<const char> &input, WorkBudget &budget);
	std::optional<DecodedFragment> take();
	void finish();

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
} // namespace detent::distribution
