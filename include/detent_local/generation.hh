#pragma once
#include "view.hh"
#include <array>
#include <filesystem>

namespace detent::local {
using GenerationId = std::array<char, 16>;
struct Descriptor {
	GenerationId generation{};
	std::uint64_t length = 0;
	std::array<char, 32> checksum{};
	distribution::Cursor cursor;
	distribution::Profile profile;
};
inline constexpr std::size_t descriptor_bytes = 176;
std::array<char, descriptor_bytes> encode_descriptor(const Descriptor &descriptor);
Descriptor decode_descriptor(std::span<const char> bytes);
std::string generation_name(const GenerationId &generation);
// Called on startup or a refresh worker; never on the hot lookup path.
// The directory must be trusted and owned by the publishing service account.
// All ReadViews retain a shared kernel lease until their mapped bytes retire.
ReadView open_generation(const std::filesystem::path &directory, ViewLimits limits = {},
						 std::optional<distribution::Cursor> expected = {});
} // namespace detent::local
