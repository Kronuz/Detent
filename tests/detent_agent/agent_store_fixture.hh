#pragma once
// Synthetic generations for the agent store: files of the right shape whose `.live` binds them.
#include "detent_local/agent_store.hh"
#include "live_record.hh"
#include "segment_names.hh"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sha256.h>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixture {
namespace l = detent::local;
namespace d = detent::distribution;
inline void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error(std::string("check failed at ") + where.file_name() + ":" +
								 std::to_string(where.line()));
}
template <class Error, class Call> void refuses(Call &&call, std::source_location where = std::source_location::current()) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	check(false, where);
}
inline std::array<char, 32> sha(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	std::array<char, 32> out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
}
inline d::Identity identity(char fill) {
	d::Identity value;
	value.fill(fill);
	return value;
}
inline l::NamespaceCursor cursor(char incarnation, std::uint64_t position) {
	return {identity('c'), identity(incarnation), position};
}
struct Segment {
	l::StorageIdentity id{};
	std::string pack;
};
// A generation of `name` at `cursor`, built on `base` (zero for a full copy), keeping `kept` of the
// base's segments and writing `fresh` new ones whose bytes depend on `seed`.
struct Generation {
	l::PreparedGeneration prepared;
	l::GenerationId id{};
	std::vector<Segment> segments; // every segment its `.live` names
};
inline Generation generation(const d::Profile &profile, std::string name, l::NamespaceCursor at,
							 const l::GenerationId &base, std::uint32_t ordinal, std::span<const Segment> kept,
							 unsigned fresh, char seed) {
	Generation g;
	g.id = l::derive_generation(profile, at.incarnation, at.position, base, ordinal);
	g.prepared.name = std::move(name);
	g.prepared.cursor = at;
	g.prepared.base = base;
	g.prepared.ordinal = ordinal;
	g.prepared.image = std::string(320, seed) + g.prepared.name;
	g.prepared.image.resize(320);
	g.prepared.read = std::string(192, char(seed + 1));
	g.segments.assign(kept.begin(), kept.end());
	for (unsigned k = 0; k < fresh; ++k) {
		Segment segment{l::derive_segment(g.id, k), std::string(700 + 13 * k, char(seed + 2 + k))};
		std::string owner(128, '\0');
		std::copy(segment.id.begin(), segment.id.end(), owner.begin());
		const auto sum = sha(segment.pack);
		std::copy(sum.begin(), sum.end(), owner.begin() + 16);
		g.prepared.segments.push_back({segment.id, segment.pack, owner});
		g.segments.push_back(std::move(segment));
	}
	std::sort(g.segments.begin(), g.segments.end(), [](const Segment &a, const Segment &b) {
		return std::memcmp(a.id.data(), b.id.data(), a.id.size()) < 0;
	});
	l::detail::LiveRecord live;
	live.generation = g.id;
	live.root_checksum = sha(g.prepared.image);
	live.read_digest = sha(g.prepared.read);
	live.histogram[0] = 1;
	for (const auto &segment : g.segments)
		live.rows.push_back({segment.id, segment.pack.size(), sha(segment.pack), {1, segment.pack.size()}});
	g.prepared.live.resize(l::detail::live_record_size(live.rows.size()));
	l::detail::encode_live_record(live, g.prepared.live);
	return g;
}
inline std::string slurp(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), {}};
}
inline std::vector<std::string> listing(const std::filesystem::path &root) {
	std::vector<std::string> names;
	for (const auto &entry : std::filesystem::directory_iterator(root))
		names.push_back(entry.path().filename().string());
	std::sort(names.begin(), names.end());
	return names;
}
inline std::string generation_file(const l::GenerationId &id, std::string_view suffix) {
	return l::generation_name(id) + std::string(suffix);
}
// Every file the durable table reaches exists and verifies: `.image` and `.read` against the table,
// `.live` against its own digest and bindings, and every pack it names against its row.
inline bool table_verifies(const std::filesystem::path &root, const l::StoreTable &table,
						   std::uint64_t maximum_segments = 1024) {
	for (const auto &entry : table.entries) {
		const auto image = slurp(root / generation_file(entry.generation, ".image"));
		const auto read = slurp(root / generation_file(entry.generation, ".read"));
		if (image.size() != entry.root_length || sha(image) != entry.root || sha(read) != entry.read)
			return false;
		try {
			const auto live = l::detail::decode_live_record(slurp(root / generation_file(entry.generation, ".live")),
															  maximum_segments);
			if (live.generation != entry.generation || live.root_checksum != entry.root || live.read_digest != entry.read)
				return false;
			for (const auto &row : live.rows) {
				const l::detail::SegmentNames names(row.segment);
				const auto pack = slurp(root / std::string(names.payload_name()));
				const auto owner = slurp(root / std::string(names.owner_name()));
				if (pack.size() != row.length || sha(pack) != row.checksum || owner.size() != 128 ||
					!std::equal(row.segment.begin(), row.segment.end(), owner.begin()) ||
					!std::equal(row.checksum.begin(), row.checksum.end(), owner.begin() + 16))
					return false;
			}
		} catch (const std::invalid_argument &) {
			return false;
		}
	}
	return true;
}
} // namespace fixture
