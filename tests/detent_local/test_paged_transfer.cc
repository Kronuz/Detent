// The dense page transfer and its verifying receiver (dense-distribution D2-D4). With no
// arguments, builds generations with the packer and checks the round trip and every refusal;
// with `--fixtures DIR [--only SLUG]`, replays the model's transfers (dense-distribution-model.py
// --out=DIR): full copy, deltas and refusal cases.
#include "dense_graph_fixture.hh"
#include "detent_local/layout.hh"
#include "paged_transfer.hh"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sha256.h>
#include <sstream>

namespace {
namespace d = detent::distribution;
namespace l = detent::local;
namespace p = l::paged;
using l::detail::TransferRefusal;
using dense_graph_test::check;
using Fixture = dense_graph_test::Fixture;

std::shared_ptr<d::StorageLedger> ledger() { return std::make_shared<d::StorageLedger>(std::size_t(8) << 30); }
l::SegmentId own_id(std::uint32_t number) {
	l::SegmentId id{};
	id[0] = 'r';
	l::write_integer(id, 12, number, 4);
	return id;
}
template <class F> void refused(TransferRefusal reason, F call, std::source_location at = std::source_location::current()) {
	try {
		call();
	} catch (const l::detail::TransferRefused &error) {
		if (error.reason() == reason)
			return;
		throw std::runtime_error("refused for another reason (" + std::string(error.what()) + ") at line " +
								 std::to_string(at.line()));
	}
	throw std::runtime_error("transfer accepted at line " + std::to_string(at.line()));
}

// --- generations built by the packer ------------------------------------------------------------

std::vector<char> definition(std::size_t i, std::size_t overrides) {
	detent::KnobDef value{static_cast<std::int64_t>(i)};
	for (std::size_t k = 0; k < overrides; ++k)
		value.overrides.emplace("group-" + std::string(48, char('a' + k % 26)) + std::to_string(k), std::int64_t(k));
	std::vector<char> body(p::definition_size(value));
	p::encode_definition(body, value, 8);
	return body;
}
// `count` definitions; every 5,000th has 120 overrides, a page alone. `changed` gets another value.
std::shared_ptr<Fixture> generation(std::size_t count, std::size_t changed = SIZE_MAX) {
	auto result = std::make_shared<Fixture>();
	std::array<char, 202> key{};
	for (std::size_t i = 0; i < count; ++i) {
		const auto length = p::encode_definition_key(key, "ns:k" + std::to_string(100000 + i));
		result->rows.push_back(
			{std::string(key.data(), length), definition(i == changed ? i + 7 : i, i % 5000 == 17 ? 120 : 0)});
	}
	const auto length = p::encode_namespace_key(key, "ns");
	std::vector<char> watermark(8);
	l::write_integer(watermark, 0, 9, 8);
	result->rows.push_back({std::string(key.data(), length), std::move(watermark)});
	return dense_graph_test::seal(std::move(result), count, 1);
}
l::detail::TransferPages reader(const Fixture &f) {
	return [&f](const p::Reference &r) { return std::span<const char>(f.bytes).subspan(r.offset, r.length); };
}
std::vector<p::Reference> seal_order(const Fixture &f) {
	std::vector<p::Reference> out;
	for (const auto &page : f.pages)
		out.push_back(page.reference);
	return out;
}
p::Limits limits() {
	p::Limits out;
	out.records = 100'000'000;
	out.namespaces = 100'000'000;
	out.pages = 100'000'000;
	out.bytes = std::uint64_t(4) << 30;
	return out;
}
l::detail::ReceivedTransfer receive(std::span<const char> stream, const l::detail::TransferBase *base,
									const p::Digest &root, std::uint64_t revision = 10, p::Limits bounds = limits()) {
	return l::detail::receive_transfer(ledger(), stream, base, {root, revision}, own_id, bounds);
}
std::span<const char> page_of(const l::detail::ReceivedTransfer &received, const p::Reference &r) {
	for (const auto &segment : received.segments)
		if (segment.ownership.segment == r.segment)
			return std::span<const char>(segment.bytes->data(), segment.bytes->size()).subspan(r.offset, r.length);
	throw std::runtime_error("no received segment");
}

// --- stream surgery ------------------------------------------------------------------------------

struct Stream {
	std::vector<char> bytes;
	std::vector<std::size_t> starts, lengths;
	explicit Stream(std::vector<char> b) : bytes(std::move(b)) {
		for (std::size_t at = 0; at < bytes.size();) {
			const auto length = l::read_integer(bytes, at + 8, 4);
			starts.push_back(at);
			lengths.push_back(length);
			at += length;
		}
	}
	std::span<char> page(std::size_t i) { return std::span<char>(bytes).subspan(starts[i], lengths[i]); }
	std::size_t externals(std::size_t i) { return l::read_integer(page(i), 12, 2); }
	std::size_t placement(std::size_t i, std::size_t j) {
		return lengths[i] - p::placement_bytes * externals(i) + p::placement_bytes * j;
	}
	std::size_t content(std::size_t i, std::size_t j) {
		return lengths[i] - p::child_bytes * externals(i) + p::content_bytes * j;
	}
	std::size_t root() const { return starts.size() - 1; }
	void place(std::size_t i, std::size_t j, l::detail::TransferPlace at) {
		l::write_integer(page(i), placement(i, j) + 12, at.segment, 4);
		l::write_integer(page(i), placement(i, j) + 16, at.offset, 4);
	}
	l::detail::TransferPlace placed(std::size_t i, std::size_t j) {
		return {std::uint32_t(l::read_integer(page(i), placement(i, j) + 12, 4)),
				std::uint32_t(l::read_integer(page(i), placement(i, j) + 16, 4))};
	}
};
std::vector<std::uint32_t> lengths_of(const Stream &s) { return {s.lengths.begin(), s.lengths.end()}; }
// A forger's layout: segments broken at 512 KiB (the review's P1).
std::vector<l::detail::TransferPlace> half_layout(std::span<const std::uint32_t> lengths) {
	std::vector<l::detail::TransferPlace> out;
	std::uint32_t number = 0;
	std::size_t used = p::segment_bytes;
	for (const auto length : lengths) {
		auto start = (used + 7) & ~std::size_t(7);
		if (start + length > p::segment_bytes / 2) {
			++number;
			start = 0;
		}
		out.push_back({number, std::uint32_t(start)});
		used = start + length;
	}
	return out;
}
// A root forgery sealed as its sender would seal it: the edit applied to the stored root and the
// wire root alike, and the stored root's digest named.
template <class Edit> p::Digest forge_root(const Fixture &f, Stream &wire, Edit edit) {
	auto stored = f.pages.back().bytes;
	edit(std::span<char>(stored));
	edit(wire.page(wire.root()));
	return p::page_commitment(stored, limits());
}

// --- hermetic checks -----------------------------------------------------------------------------

void round_trip(const Fixture &f) {
	const auto order = seal_order(f);
	const auto wire = l::detail::make_transfer(order, reader(f), limits());
	std::uint64_t page_bytes = 0;
	for (const auto &page : f.pages)
		page_bytes += page.bytes.size() - p::placement_bytes * l::read_integer(page.bytes, 12, 2) - 32;
	const auto before = p::checked_hash_counts();
	const auto got = receive(wire, nullptr, f.root.tree.digest);
	const auto hashed = p::checked_hash_counts() - before;
	check(got.tree.digest == f.root.tree.digest && got.tree.keys == f.root.tree.keys &&
		  got.pages == f.pages.size() && got.base_children == 0);
	std::vector<std::uint32_t> lengths;
	for (const auto &r : order)
		lengths.push_back(r.length);
	const auto places = l::detail::transfer_layout(lengths);
	check(got.segments.size() == places.back().segment);
	std::uint64_t pack_bytes = 0;
	std::vector<p::Reference> received;
	for (std::size_t i = 0; i < f.pages.size(); ++i) {
		auto r = f.pages[i].reference;
		r.segment = own_id(places[i].segment);
		r.offset = places[i].offset;
		received.push_back(r);
		const auto bytes = page_of(got, r);
		const auto &source = f.pages[i].bytes;
		const auto covered = source.size() - p::placement_bytes * l::read_integer(source, 12, 2);
		check(bytes.size() == source.size() && std::equal(source.begin(), source.begin() + covered, bytes.begin()));
		p::verify_page(bytes, r, limits());
	}
	for (std::size_t s = 0; s < got.segments.size(); ++s) {
		const auto &segment = got.segments[s];
		check(segment.ownership.segment == own_id(std::uint32_t(s + 1)) &&
			  segment.ownership.length == segment.bytes->size() &&
			  segment.ownership.checksum == p::pack_commitment(segment.bytes->view()));
		pack_bytes += segment.bytes->size();
	}
	check(got.tree == received.back());
	check(hashed.page_calls == f.pages.size() && hashed.page_bytes == page_bytes &&
		  hashed.pack_calls == got.segments.size() && hashed.pack_bytes == pack_bytes);
	// The received generation's transfer is the same stream.
	const auto again = l::detail::make_transfer(received, [&got](const p::Reference &r) { return page_of(got, r); }, limits());
	check(again == wire);
	std::cout << "round trip: " << got.pages << " pages in " << got.segments.size() << " segments, page bytes hashed "
			  << hashed.page_bytes << ", pack bytes " << hashed.pack_bytes << "\n";
}

void refusals(const Fixture &f) {
	const auto order = seal_order(f);
	const auto root = f.root.tree.digest;
	const Stream wire(l::detail::make_transfer(order, reader(f), limits()));
	const auto honest = receive(wire.bytes, nullptr, root);
	// The digests form: every digest kept.
	Stream digests = wire;
	{
		std::vector<std::uint32_t> lengths = lengths_of(wire);
		const auto places = l::detail::transfer_layout(lengths);
		for (std::size_t i = 0; i < digests.starts.size(); ++i) {
			const auto got = page_of(honest, {own_id(places[i].segment), places[i].offset, lengths[i], 1, 1, {}});
			auto page = digests.page(i);
			std::copy_n(got.begin() + 32, 32, page.begin() + 32);
			for (std::size_t j = 0; j < digests.externals(i); ++j)
				std::copy_n(got.begin() + digests.content(i, j), 32, page.begin() + digests.content(i, j));
		}
		check(receive(digests.bytes, nullptr, root).tree.digest == root);
	}
	// Damage: a leaf body byte (the last node byte of a page whose last node is a leaf).
	std::size_t leafy = SIZE_MAX;
	for (std::size_t i = wire.starts.size() / 2; i < wire.starts.size() && leafy == SIZE_MAX; ++i) {
		const auto &bytes = f.pages[i].bytes;
		const auto end = bytes.size() - p::child_bytes * l::read_integer(bytes, 12, 2);
		for (p::NodeAt at; at.offset() < end;) {
			const auto node = at.decode(bytes, limits());
			at = p::NodeAt::after(node);
			if (at.offset() == end && node.kind != p::PageKind::Branch)
				leafy = i;
		}
	}
	check(leafy != SIZE_MAX);
	for (auto *form : {&wire, &digests}) {
		auto flipped = *form;
		const auto at = flipped.content(leafy, 0) - 1;
		flipped.page(leafy)[at] ^= 1;
		refused(form == &wire ? TransferRefusal::RootDigest : TransferRefusal::PageDigest,
				[&] { receive(flipped.bytes, nullptr, root); });
	}
	// Reuse: the root's first placement redirected to the first page; to itself; its second to its first.
	const auto rootless = wire.root();
	{
		auto s = wire;
		check(s.placed(rootless, 0) != l::detail::TransferPlace{1, 0});
		s.place(rootless, 0, {1, 0});
		refused(TransferRefusal::Unreferenced, [&] { receive(s.bytes, nullptr, root); });
		s = wire;
		const auto self = l::detail::transfer_layout(lengths_of(s)).back();
		s.place(rootless, 0, self);
		refused(TransferRefusal::ParentFirst, [&] { receive(s.bytes, nullptr, root); });
		s = wire;
		check(s.externals(rootless) >= 2);
		s.place(rootless, 1, s.placed(rootless, 0));
		refused(TransferRefusal::Twice, [&] { receive(s.bytes, nullptr, root); });
	}
	// Reuse: an extra, unreferenced copy of a leaf page first.
	{
		std::size_t leaf = 0;
		while (l::read_integer(f.pages[leaf].bytes, 12, 2))
			++leaf;
		auto twice = order;
		twice.insert(twice.begin(), order[leaf]);
		const auto stream = l::detail::make_transfer(twice, reader(f), limits());
		refused(TransferRefusal::Unreferenced, [&] { receive(stream, nullptr, root); });
	}
	// Order and truncation: the root first, the root dropped, one byte short.
	{
		const auto tail = wire.lengths.back();
		std::vector<char> first(wire.bytes.end() - tail, wire.bytes.end());
		first.insert(first.end(), wire.bytes.begin(), wire.bytes.end() - tail);
		refused(TransferRefusal::Layout, [&] { receive(first, nullptr, root); });
		const std::vector<char> dropped(wire.bytes.begin(), wire.bytes.end() - tail);
		refused(TransferRefusal::Unreferenced, [&] { receive(dropped, nullptr, root); });
		const std::vector<char> short_one(wire.bytes.begin(), wire.bytes.end() - 1);
		refused(TransferRefusal::Framing, [&] { receive(short_one, nullptr, root); });
	}
	// P1: placement naming the sender's 512 KiB segment breaks.
	{
		auto s = wire;
		const auto lengths = lengths_of(s);
		const auto derived = l::detail::transfer_layout(lengths), half = half_layout(lengths);
		check(derived != half);
		for (std::size_t i = 0; i < s.starts.size(); ++i)
			for (std::size_t j = 0; j < s.externals(i); ++j) {
				const auto at = std::find(derived.begin(), derived.end(), s.placed(i, j));
				check(at != derived.end());
				s.place(i, j, half[std::size_t(at - derived.begin())]);
			}
		refused(TransferRefusal::Layout, [&] { receive(s.bytes, nullptr, root); });
	}
	// P2: children first, not in seal order (a depth-first postorder, last child first).
	{
		std::map<std::pair<l::SegmentId, std::uint32_t>, std::size_t> index;
		for (std::size_t i = 0; i < order.size(); ++i)
			index[{order[i].segment, order[i].offset}] = i;
		std::vector<std::size_t> reverse;
		std::vector<std::pair<std::size_t, bool>> stack{{order.size() - 1, false}};
		while (!stack.empty()) {
			const auto [i, expanded] = stack.back();
			stack.pop_back();
			if (expanded) {
				reverse.push_back(i);
				continue;
			}
			stack.push_back({i, true});
			const auto &bytes = f.pages[i].bytes;
			for (std::uint16_t j = 0; j < l::read_integer(bytes, 12, 2); ++j) {
				const auto child = p::decode_external(bytes, j, limits());
				stack.push_back({index.at({child.segment, child.offset}), false});
			}
		}
		std::vector<p::Reference> other;
		for (const auto i : reverse)
			other.push_back(order[i]);
		check(other.size() == order.size() && other != order);
		const auto stream = l::detail::make_transfer(other, reader(f), limits());
		refused(TransferRefusal::Order, [&] { receive(stream, nullptr, root); });
	}
	// Sealed forgeries of the root: every digest matches, the named root is the forgery's.
	{
		auto s = wire;
		const auto keys = l::read_integer(s.page(s.root()), 16, 8);
		const auto named = forge_root(f, s, [&](std::span<char> page) { l::write_integer(page, 16, keys + 1, 8); });
		refused(TransferRefusal::Page, [&] { receive(s.bytes, nullptr, named); });
		auto t = digests;
		const auto again = forge_root(f, t, [&](std::span<char> page) { l::write_integer(page, 16, keys + 1, 8); });
		std::copy(again.begin(), again.end(), t.page(t.root()).begin() + 32);
		refused(TransferRefusal::Page, [&] { receive(t.bytes, nullptr, again); });
	}
	{
		auto s = wire;
		const auto height = static_cast<unsigned char>(s.page(s.root())[24]);
		const auto named = forge_root(f, s, [&](std::span<char> page) { page[24] = char(height + 1); });
		refused(TransferRefusal::Page, [&] { receive(s.bytes, nullptr, named); });
	}
	{
		auto s = wire;
		const auto top = s.page(s.root()).subspan(64);
		check(top[0] == 1 && static_cast<unsigned char>(top[2]) >= 1);
		const auto labels = 64 + 3 + static_cast<unsigned char>(top[1]);
		const auto named = forge_root(f, s, [&](std::span<char> page) { page[labels + 1] = page[labels]; });
		refused(TransferRefusal::Page, [&] { receive(s.bytes, nullptr, named); });
	}
	{
		auto s = wire;
		const auto nodes = l::read_integer(s.page(s.root()), 14, 2);
		const auto named = forge_root(f, s, [&](std::span<char> page) { l::write_integer(page, 14, nodes + 1, 2); });
		refused(TransferRefusal::Page, [&] { receive(s.bytes, nullptr, named); });
	}
	// Content entries that disagree with the child: keys, and (digests form) the digest.
	{
		auto s = wire;
		const auto at = s.content(s.root(), 0) + 32;
		l::write_integer(s.page(s.root()), at, l::read_integer(s.page(s.root()), at, 8) + 1, 8);
		refused(TransferRefusal::ContentSummary, [&] { receive(s.bytes, nullptr, root); });
		auto t = digests;
		t.page(t.root())[t.content(t.root(), 0)] ^= 1;
		refused(TransferRefusal::ContentDigest, [&] { receive(t.bytes, nullptr, root); });
	}
	// Leaves against the frame's position, limits and the empty transfer.
	refused(TransferRefusal::Leaf, [&] { receive(wire.bytes, nullptr, root, 7); });
	auto few = limits();
	few.pages = 2;
	refused(TransferRefusal::Limits, [&] { receive(wire.bytes, nullptr, root, 10, few); });
	check(receive({}, nullptr, p::empty_commitment()).tree == p::Reference{});
	const l::detail::TransferBase base{f.root.tree, reader(f)};
	const auto same = receive({}, &base, root);
	check(same.tree == f.root.tree && same.segments.empty());
	refused(TransferRefusal::Empty, [&] { receive({}, &base, p::empty_commitment()); });
	refused(TransferRefusal::Empty, [&] { receive({}, nullptr, root); });
	std::cout << "refusals: every case refused for its reason\n";
}

void delta(const Fixture &before, std::size_t count, std::size_t changed) {
	const auto after = generation(count, changed);
	std::map<std::string, p::Reference> by_path;
	for (const auto &page : before.pages)
		by_path[page.path] = page.reference;
	const auto same = [](const p::Reference &a, const p::Reference &b) {
		return a.digest == b.digest && a.keys == b.keys && a.length == b.length && a.height == b.height;
	};
	std::vector<p::Reference> chosen;
	for (const auto &page : after->pages) {
		const auto found = by_path.find(page.path);
		if (found == by_path.end() || !same(found->second, page.reference))
			chosen.push_back(page.reference);
	}
	check(!chosen.empty() && chosen.size() < after->pages.size());
	const auto stream = l::detail::make_transfer(chosen, reader(*after), limits());
	const l::detail::TransferBase base{before.root.tree, reader(before)};
	const auto got = receive(stream, &base, after->root.tree.digest);
	check(got.tree.digest == after->root.tree.digest && got.pages == chosen.size() && got.base_children > 0);
	// No base: a zero placement names no page; another base: it lacks the page.
	refused(TransferRefusal::Layout, [&] { receive(stream, nullptr, after->root.tree.digest); });
	const auto small = generation(40);
	const l::detail::TransferBase other{small->root.tree, reader(*small)};
	refused(TransferRefusal::BaseLacks, [&] { receive(stream, &other, after->root.tree.digest); });
	// A base child's stated digest flipped.
	Stream s(stream);
	bool flipped = false;
	for (std::size_t i = 0; i < s.starts.size() && !flipped; ++i)
		for (std::size_t j = 0; j < s.externals(i) && !flipped; ++j)
			if (s.placed(i, j) == l::detail::TransferPlace{}) {
				s.page(i)[s.content(i, j)] ^= 1;
				flipped = true;
			}
	check(flipped);
	refused(TransferRefusal::ContentDigest, [&] { receive(s.bytes, &base, after->root.tree.digest); });
	std::cout << "delta: " << got.pages << " of " << after->pages.size() << " pages sent, " << got.base_children
			  << " base children resolved, " << got.base_pages_walked << " base pages walked\n";
}

// --- the model's fixtures --------------------------------------------------------------------------

std::vector<char> load(const std::string &path) {
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::runtime_error("cannot open " + path);
	return {std::istreambuf_iterator<char>(file), {}};
}
std::vector<std::vector<std::string>> manifest(const std::string &path) {
	std::ifstream file(path);
	std::vector<std::vector<std::string>> out;
	for (std::string line; std::getline(file, line);) {
		std::istringstream words(line);
		out.emplace_back();
		for (std::string word; words >> word;)
			out.back().push_back(word);
	}
	return out;
}
std::vector<char> unhex(const std::string &hex) {
	std::vector<char> out;
	for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
		out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	return out;
}
template <class T> T from_hex(const std::string &hex) {
	const auto bytes = unhex(hex);
	T out{};
	check(bytes.size() == out.size());
	std::copy(bytes.begin(), bytes.end(), out.begin());
	return out;
}
// The model's segment id for ordinal n: SHA-256("model segment" || n as 8 bytes)[0, 16).
l::SegmentId model_id(std::uint64_t ordinal) {
	std::array<char, 21> input{};
	std::memcpy(input.data(), "model segment", 13);
	l::write_integer(input, 13, ordinal, 8);
	SHA256 hash;
	hash.add(input.data(), input.size());
	std::array<unsigned char, 32> digest{};
	hash.getHash(digest.data());
	l::SegmentId id;
	std::copy_n(digest.begin(), 16, id.begin());
	return id;
}
std::string hex(std::span<const char> bytes) {
	static constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (const auto byte : bytes) {
		out.push_back(digits[static_cast<unsigned char>(byte) >> 4]);
		out.push_back(digits[static_cast<unsigned char>(byte) & 15]);
	}
	return out;
}
std::map<std::string, std::string> fields(const std::vector<std::vector<std::string>> &lines) {
	std::map<std::string, std::string> out;
	for (const auto &line : lines)
		if (line.size() == 2)
			out[line[0]] = line[1];
	return out;
}

void model_full(const std::string &dir) {
	const auto lines = manifest(dir + "/full.txt");
	auto values = fields(lines);
	const auto stream = load(dir + "/full.transfer");
	const auto root = from_hex<p::Digest>(values.at("root"));
	const auto before = p::checked_hash_counts();
	const auto got = l::detail::receive_transfer(ledger(), stream, nullptr, {root, std::stoull(values.at("revision"))},
												 [](std::uint32_t n) { return model_id(n - 1); }, limits());
	const auto hashed = p::checked_hash_counts() - before;
	check(got.tree.digest == root && got.pages == std::stoull(values.at("pages")) &&
		  got.segments.size() == std::stoull(values.at("segments")));
	std::size_t packs = 0;
	for (const auto &line : lines) {
		if (line.empty() || line[0] != "pack")
			continue;
		const auto n = std::stoull(line[1]);
		const auto &segment = got.segments.at(n - 1);
		check(hex(segment.ownership.segment) == line[2] && segment.ownership.segment == model_id(n - 1));
		const auto expected = load(dir + "/full/pack-" + line[1]);
		check(segment.bytes->view() == std::string_view(expected.data(), expected.size()));
		++packs;
	}
	check(packs == got.segments.size());
	check(hashed.page_bytes == std::stoull(values.at("page_hashed")) &&
		  hashed.pack_bytes == std::stoull(values.at("pack_bytes")) && hashed.page_calls == got.pages &&
		  hashed.pack_calls == got.segments.size());
	std::cout << "model full copy: root " << values.at("root") << " equal; " << packs
			  << " packs byte for byte with the model's ids; page bytes hashed " << hashed.page_bytes
			  << ", pack bytes hashed " << hashed.pack_bytes << "\n";
}

void model_deltas(const std::string &dir) {
	const auto base_lines = manifest(dir + "/base.txt");
	std::map<l::SegmentId, std::vector<char>> packs;
	p::Reference root;
	for (const auto &line : base_lines) {
		if (line.empty())
			continue;
		if (line[0] == "pack")
			packs[from_hex<l::SegmentId>(line[2])] = load(dir + "/base/pack-" + line[1]);
		else if (line[0] == "base") {
			root.segment = from_hex<l::SegmentId>(line[1]);
			root.offset = std::stoul(line[2]);
			root.length = std::stoul(line[3]);
			root.keys = std::stoull(line[4]);
			root.height = std::stoul(line[5]);
			root.digest = from_hex<p::Digest>(line[6]);
		}
	}
	const l::detail::TransferBase base{root, [&packs](const p::Reference &r) {
										   const auto &pack = packs.at(r.segment);
										   return std::span<const char>(pack).subspan(r.offset, r.length);
									   }};
	for (const auto &line : manifest(dir + "/delta.txt")) {
		if (line.size() != 8 || line[0] != "delta")
			continue;
		const auto stream = load(dir + "/delta-" + line[1] + ".transfer");
		const auto before = p::checked_hash_counts();
		const auto got =
			l::detail::receive_transfer(ledger(), stream, &base, {from_hex<p::Digest>(line[2]), std::stoull(line[3])},
										[](std::uint32_t n) { return model_id(1'000'000 + n); }, limits());
		const auto hashed = p::checked_hash_counts() - before;
		check(hex(got.tree.digest) == line[2] && got.pages == std::stoull(line[4]) &&
			  hashed.page_bytes == std::stoull(line[5]) && got.base_children == std::stoull(line[6]));
		std::cout << "model delta " << line[1] << ": root " << line[2] << " equal; " << got.pages << " pages, "
				  << hashed.page_bytes << " page bytes hashed, " << got.base_children << " base children, "
				  << got.base_pages_walked << " base pages walked (model " << line[7] << ")\n";
	}
}

TransferRefusal expected_reason(const std::string &text) {
	const std::pair<const char *, TransferRefusal> table[] = {
		{"root digest mismatch", TransferRefusal::RootDigest},
		{"page digest mismatch", TransferRefusal::PageDigest},
		{"no parent references", TransferRefusal::Unreferenced},
		{"placement names no page", TransferRefusal::Layout},
		{"page length", TransferRefusal::Framing},
		{"seal order", TransferRefusal::Order},
		{"header keys", TransferRefusal::Page},
		{"header height", TransferRefusal::Page},
		{"branch labels", TransferRefusal::Page},
		{"page structure", TransferRefusal::Page},
		{"not packed by D5", TransferRefusal::Page},
		{"empty transfer", TransferRefusal::Empty},
	};
	for (const auto &[needle, reason] : table)
		if (text.find(needle) != std::string::npos)
			return reason;
	throw std::runtime_error("no refusal for the model's " + text);
}

void model_cases(const std::string &dir, const std::string &only) {
	std::size_t ran = 0;
	for (const auto &line : manifest(dir + "/cases.txt")) {
		if (line.size() < 6 || line[0] != "case" || (!only.empty() && line[1] != only))
			continue;
		std::string expect;
		for (std::size_t w = 5; w < line.size(); ++w)
			expect += (w > 5 ? " " : "") + line[w];
		const auto stream = load(dir + "/case-" + line[1] + ".transfer");
		const auto reason = expected_reason(expect);
		try {
			refused(reason, [&] {
				l::detail::receive_transfer(ledger(), stream, nullptr, {from_hex<p::Digest>(line[2]), std::stoull(line[3])},
											[](std::uint32_t n) { return model_id(n - 1); }, limits());
			});
		} catch (const std::exception &error) {
			throw std::runtime_error("model case " + line[1] + ": " + error.what());
		}
		std::cout << "model case " << line[1] << ": refused (" << expect << "), check " << line[4] << "\n";
		++ran;
	}
	check(ran > 0);
}
} // namespace

int main(int argc, char **argv) {
	try {
		const std::vector<std::string> args(argv + 1, argv + argc);
		if (args.size() >= 2 && args[0] == "--fixtures") {
			const auto only = args.size() == 4 && args[2] == "--only" ? args[3] : std::string();
			if (only.empty()) {
				model_full(args[1]);
				if (std::ifstream(args[1] + "/delta.txt"))
					model_deltas(args[1]);
			}
			if (std::ifstream(args[1] + "/cases.txt"))
				model_cases(args[1], only);
			return 0;
		}
		constexpr std::size_t count = 20000;
		const auto f = generation(count);
		round_trip(*f);
		refusals(*f);
		delta(*f, count, 12345);
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "test_paged_transfer: " << error.what() << "\n";
		return 1;
	}
}
