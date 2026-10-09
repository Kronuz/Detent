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
// Reusable owner-local sender. Output and static codec workspace are physically
// admitted before accepting input; only sealed owned frames cross to transport.
class OwnedFragmentEncoder {
  public:
	OwnedFragmentEncoder(UpdateKind, Identity update, std::uint64_t total, bool compress,
						 std::shared_ptr<StorageLedger>);
	~OwnedFragmentEncoder();
	void begin(std::shared_ptr<const OwnedBuffer> body, bool final);
	void step(WorkBudget &);
	bool ready() const noexcept;
	std::shared_ptr<const OwnedBuffer> take();
	bool discard(WorkBudget &) noexcept;
	static std::size_t maximum_wire_bytes() noexcept;

  private:
	struct Impl;
	StorageOwner<Impl> impl_;
};
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
