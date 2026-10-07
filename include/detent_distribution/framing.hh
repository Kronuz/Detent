#pragma once
#include "protocol.hh"
#include <optional>

namespace detent::distribution {
inline constexpr std::size_t frame_header_bytes = 96, fragment_bytes = 65536;
enum class UpdateKind : std::uint8_t { Snapshot = 1, Delta = 2, Progress = 3 };
struct Fragment {
	UpdateKind kind = UpdateKind::Snapshot;
	Identity update{};
	std::uint64_t offset = 0, total = 0;
	bool final = false;
	std::string bytes;
};
std::string encode_fragment(const Fragment &fragment, bool compress = false);
class FragmentDecoder {
  public:
	FragmentDecoder(UpdateKind expected_kind, std::uint64_t maximum_bytes, bool allow_compression = false);
	~FragmentDecoder();
	FragmentDecoder(FragmentDecoder &&) noexcept;
	FragmentDecoder(const FragmentDecoder &) = delete;
	// One output slot. Stops consumption while a complete fragment awaits take().
	void feed(std::span<const char> &input, WorkBudget &budget);
	std::optional<Fragment> take();
	void finish();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::distribution
