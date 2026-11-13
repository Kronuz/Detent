#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include <algorithm>
#include <limits>
#include <sha256.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace detent::local {
namespace {
// Appends each field to one SHA-256 input and returns its first 16 bytes.
class Input {
  public:
	explicit Input(std::string_view domain) : bytes_(domain) {}
	template <std::size_t N> Input &add(const std::array<char, N> &value) {
		bytes_.append(value.data(), N);
		return *this;
	}
	Input &add(std::uint64_t value, unsigned width) {
		const auto at = bytes_.size();
		bytes_.resize(at + width);
		write_integer(std::span<char>(bytes_), at, value, width);
		return *this;
	}
	std::array<char, 16> id() const {
		SHA256 hash;
		hash.add(bytes_.data(), bytes_.size());
		std::array<char, 32> sum{};
		hash.getHash(reinterpret_cast<unsigned char *>(sum.data()));
		std::array<char, 16> out{};
		std::copy_n(sum.begin(), out.size(), out.begin());
		return out;
	}

  private:
	std::string bytes_;
};
} // namespace

GenerationId derive_generation(const distribution::Profile &profile, const distribution::Identity &incarnation,
							   std::uint64_t position, const GenerationId &base, std::uint32_t ordinal) {
	return Input("detent.agent-store.generation")
		.add(agent_store_layout, 4)
		.add(profile.semantic_id, 4)
		.add(profile.required_capabilities, 8)
		.add(incarnation)
		.add(position, 8)
		.add(base)
		.add(ordinal, 4)
		.id();
}

SegmentId derive_segment(const GenerationId &generation, std::uint64_t ordinal) {
	return Input(segment_domain).add(generation).add(ordinal, 8).id();
}

distribution::NamespaceCursor namespace_cursor(const distribution::Cursor &cursor) {
	const auto incarnation = Input("detent.agent-store.incarnation")
								 .add(cursor.cluster)
								 .add(cursor.group)
								 .add(cursor.restore_epoch)
								 .add(cursor.scope)
								 .id();
	return {cursor.cluster, incarnation, cursor.revision};
}

std::uint32_t build_ordinal(const std::optional<Descriptor> &selected, const distribution::Cursor &target) {
	if (!selected || selected->cursor != target)
		return 0;
	if (selected->ordinal == std::numeric_limits<std::uint32_t>::max())
		throw std::overflow_error("build ordinal exhausted at an unchanged head");
	return selected->ordinal + 1;
}

std::string namespace_name(const distribution::Cursor &cursor) {
	static constexpr char hex[] = "0123456789abcdef";
	std::string name;
	name.reserve(cursor.scope.size() * 2);
	for (unsigned char byte : cursor.scope) {
		name.push_back(hex[byte >> 4]);
		name.push_back(hex[byte & 15]);
	}
	return name;
}
DerivedGeneration next_generation(const std::optional<Descriptor> &selected, const distribution::Cursor &target,
								  const distribution::Profile &profile, bool full_copy) {
	if (!full_copy && !selected)
		throw std::invalid_argument("a build from a base needs the selected generation");
	const auto at = namespace_cursor(target);
	const auto ordinal = build_ordinal(selected, target);
	const GenerationId base = full_copy ? GenerationId{} : selected->generation;
	return {derive_generation(profile, at.incarnation, at.position, base, ordinal), ordinal};
}
} // namespace detent::local
