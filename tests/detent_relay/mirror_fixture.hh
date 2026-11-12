#pragma once
// A scripted upstream for the relay mirror: per namespace, D6's tiers moved by seals, roll-ups,
// squashes, cuts, policy changes and new incarnations. A file's bytes are a function of its name, as
// blocks and bases are immutable by name; the mirror never parses them.
#include "detent_relay/mirror.hh"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sha256.h>
#include <source_location>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace fixture {
namespace r = detent::relay;
namespace c = kronuz::io::completion;
inline void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error(std::string("check failed at ") + where.file_name() + ":" + std::to_string(where.line()));
}
template <class Error, class Call> void refuses(Call &&call, std::source_location where = std::source_location::current()) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	check(false, where);
}
inline r::Digest sha(std::string_view bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	r::Digest out{};
	hash.getHash(reinterpret_cast<unsigned char *>(out.data()));
	return out;
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
inline void write_file(const std::filesystem::path &path, std::string_view bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), std::streamsize(bytes.size()));
	out.close();
	check(bool(out) && ::chmod(path.c_str(), 0644) == 0);
}
inline void prepare(const std::filesystem::path &root) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	check(::chmod(root.c_str(), 0700) == 0);
}
inline std::shared_ptr<c::PosixIO> io_at(const std::filesystem::path &root) {
	return std::make_shared<c::PosixIO>(root, 0644);
}
// Deterministic bytes for a name: 40 to 1,063 bytes of a SHA-256 chain.
inline std::string bytes_of(std::string_view name) {
	auto block = sha(name);
	std::string out;
	const std::size_t size = 40 + ((static_cast<unsigned char>(block[0]) << 2) | (static_cast<unsigned char>(block[1]) & 3));
	while (out.size() < size) {
		out.append(block.data(), block.size());
		block = sha(std::string_view(block.data(), block.size()));
	}
	out.resize(size);
	return out;
}
inline r::MirrorFile file(r::MirrorFile::Kind kind, std::string name, std::uint64_t first, std::uint64_t last) {
	const auto bytes = bytes_of(name);
	return {kind, std::move(name), first, last, bytes.size(), sha(bytes)};
}
inline detent::distribution::Identity identity(char fill) {
	detent::distribution::Identity value;
	value.fill(fill);
	return value;
}

// One namespace's history on the upstream: the cut, the blocks above it and the level-0 sections.
struct History {
	std::string name;
	char incarnation = 'a';
	std::uint64_t cut = 0;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> blocks, sections;
	std::vector<std::uint64_t> section_seals;
	bool copy = false;
	std::uint64_t end() const {
		if (!sections.empty())
			return sections.back().second;
		return blocks.empty() ? cut : blocks.back().second;
	}
	std::string stem() const { return name + "-" + std::string(1, incarnation); }
};

class Upstream {
  public:
	std::uint64_t seal = 0, ordinal = 0;
	std::string policy = "policy-1";
	std::map<std::string, History> histories;
	std::uint64_t fetched_files = 0, fetched_bytes = 0;
	bool reachable = true;

	History &add(const std::string &name) {
		auto &h = histories[name] = History{};
		h.name = name;
		return h;
	}
	// A seal: `positions` new positions in each named namespace, as one level-0 section each.
	void advance(std::initializer_list<std::pair<std::string, std::uint64_t>> writes) {
		++seal;
		++ordinal;
		for (const auto &[name, positions] : writes) {
			auto &h = histories.at(name);
			const auto first = h.end() + 1;
			h.sections.emplace_back(first, first + positions - 1);
			h.section_seals.push_back(seal);
		}
	}
	// A roll-up: the level-0 sections merge into one block.
	void roll_up(const std::string &name) {
		auto &h = histories.at(name);
		if (h.sections.empty())
			return;
		h.blocks.emplace_back(h.sections.front().first, h.sections.back().second);
		h.sections.clear();
		h.section_seals.clear();
	}
	// A squash: the two newest blocks merge.
	void squash(const std::string &name) {
		auto &h = histories.at(name);
		check(h.blocks.size() >= 2);
		const auto younger = h.blocks.back();
		h.blocks.pop_back();
		h.blocks.back().second = younger.second;
	}
	// A cut: the oldest block joins the base.
	void cut(const std::string &name) {
		auto &h = histories.at(name);
		check(h.blocks.size() >= 2);
		h.cut = h.blocks.front().second;
		h.blocks.erase(h.blocks.begin());
	}
	// A new incarnation: an empty history under another identity, from a base at its position.
	void reincarnate(const std::string &name, char incarnation, std::uint64_t position) {
		auto &h = histories.at(name);
		h = History{};
		h.name = name;
		h.incarnation = incarnation;
		h.cut = position;
	}
	r::NamespaceLayout layout(const std::string &name) const {
		const auto &h = histories.at(name);
		r::NamespaceLayout out;
		out.name = h.name;
		out.incarnation = identity(h.incarnation);
		out.seal = seal;
		out.ordinal = ordinal;
		out.policy = policy;
		using K = r::MirrorFile::Kind;
		out.files.push_back(file(K::Base, "base-" + h.stem() + "-" + std::to_string(h.cut), 0, h.cut));
		for (const auto &[a, b] : h.blocks)
			out.files.push_back(file(K::Block, "block-" + h.stem() + "-" + std::to_string(a) + "-" + std::to_string(b), a, b));
		for (std::size_t i = 0; i < h.sections.size(); ++i)
			out.files.push_back(file(K::Section, "seal-" + std::to_string(h.section_seals[i]) + "#" + h.stem(),
									 h.sections[i].first, h.sections[i].second));
		if (h.copy)
			out.files.push_back(file(K::Copy, "copy-" + h.stem() + "-" + std::to_string(h.end()), 0, h.end()));
		return out;
	}
	std::vector<r::NamespaceLayout> layouts() const {
		std::vector<r::NamespaceLayout> out;
		for (const auto &[name, h] : histories)
			out.push_back(layout(name));
		return out;
	}
	// The upstream's fetch by name: counts what it serves.
	r::MirrorFetch fetch() {
		return [this](const r::MirrorFile &f) {
			if (!reachable)
				throw std::runtime_error("upstream unreachable");
			++fetched_files;
			auto bytes = bytes_of(f.name);
			fetched_bytes += bytes.size();
			return bytes;
		};
	}
};

// Every mirror name the table reaches, with `mirror` and `owner.lock`.
inline std::vector<std::string> expected_listing(const r::MirrorTable &table) {
	std::vector<std::string> names{"mirror", "owner.lock"};
	for (const auto &entry : table.entries)
		for (const auto &f : entry.files)
			names.push_back(r::mirror_file_name(f.name));
	std::sort(names.begin(), names.end());
	return names;
}
// The directory holds exactly the reached files, each with the upstream's bytes.
inline bool mirror_matches(const std::filesystem::path &root, const r::MirrorTable &table) {
	if (listing(root) != expected_listing(table))
		return false;
	for (const auto &entry : table.entries)
		for (const auto &f : entry.files)
			if (slurp(root / r::mirror_file_name(f.name)) != bytes_of(f.name))
				return false;
	return true;
}
} // namespace fixture
