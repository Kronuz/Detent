#pragma once
#include "detent_local/generation.hh"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace detent::local::detail {
enum class GenerationFile { Lease, Image, Descriptor, ReaderMetadata };
struct NamedGeneration {
	GenerationId generation;
	GenerationFile kind;
};
inline std::string_view suffix(GenerationFile kind) {
	switch (kind) {
	case GenerationFile::Lease:
		return ".lease";
	case GenerationFile::Image:
		return ".image";
	case GenerationFile::Descriptor:
		return ".descriptor";
	case GenerationFile::ReaderMetadata:
		return ".read";
	}
	throw std::invalid_argument("unsupported generation file kind");
}
class GenerationName {
  public:
	GenerationName(const GenerationId &id, GenerationFile kind) : GenerationName(id, suffix(kind)) {}
	GenerationName(const GenerationId &id, std::string_view ending) {
		size_ = generation_name(id, bytes_).size();
		if (ending.size() > bytes_.size() - size_)
			throw std::length_error("generation name exceeds fixed capacity");
		std::copy(ending.begin(), ending.end(), bytes_.begin() + size_);
		size_ += ending.size();
	}
	std::string_view view() const noexcept { return {bytes_.data(), size_}; }

  private:
	std::array<char, 64> bytes_;
	std::size_t size_ = 0;
};
// Syntactic canonicality only: even a zero-identity anchor counts toward
// inventory debt. Ownership codecs separately reject that generation.
inline std::optional<NamedGeneration> parse_generation_name(std::string_view name) noexcept {
	constexpr std::string_view prefix = "generation-";
	constexpr auto base_length = prefix.size() + 32;
	if (!name.starts_with(prefix) || name.size() < base_length)
		return {};
	GenerationFile kind;
	const auto ending = name.substr(base_length);
	if (ending == ".lease")
		kind = GenerationFile::Lease;
	else if (ending == ".image")
		kind = GenerationFile::Image;
	else if (ending == ".descriptor")
		kind = GenerationFile::Descriptor;
	else if (ending == ".read")
		kind = GenerationFile::ReaderMetadata;
	else
		return {};
	NamedGeneration parsed{{}, kind};
	auto digit = [](unsigned char value) -> int {
		if (value >= '0' && value <= '9')
			return value - '0';
		if (value >= 'a' && value <= 'f')
			return value - 'a' + 10;
		return -1;
	};
	for (std::size_t at = 0; at < parsed.generation.size(); ++at) {
		auto high = digit(name[prefix.size() + at * 2]);
		auto low = digit(name[prefix.size() + at * 2 + 1]);
		if (high < 0 || low < 0)
			return {};
		parsed.generation[at] = static_cast<char>((high << 4) | low);
	}
	return parsed;
}
} // namespace detent::local::detail
