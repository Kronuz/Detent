// The relay mirror (namespace-delivery D7): the table codec, the tiers, following the upstream's
// layout (cuts, squashes, policy, incarnations, drops), restart without re-fetching, repair by name,
// and a relay chain byte-identical at every hop.
#include "mirror_fixture.hh"
#include <cstdio>

using namespace fixture;
namespace {
using K = r::MirrorFile::Kind;

// Every file the table reaches, read through the mirror: its name and bytes.
std::vector<std::string> reads(r::MirrorStore &store, const r::MirrorFetch &fetch) {
	std::vector<std::string> out;
	for (const auto &layout : store.table().entries)
		for (const auto &f : layout.files)
			out.push_back(f.name + "=" + store.read(f, fetch));
	return out;
}
std::uint64_t bytes_of_new(const r::MirrorTable &before, const std::vector<r::NamespaceLayout> &after, std::uint64_t &files) {
	std::set<std::string> held;
	for (const auto &e : before.entries)
		for (const auto &f : e.files)
			held.insert(f.name);
	std::uint64_t bytes = 0;
	files = 0;
	for (const auto &e : after)
		for (const auto &f : e.files)
			if (!held.contains(f.name)) {
				bytes += f.size;
				++files;
			}
	return bytes;
}

void codec() {
	Upstream up;
	up.add("app/flags");
	up.add("beta");
	up.advance({{"app/flags", 3}, {"beta", 1}});
	up.roll_up("app/flags");
	up.advance({{"app/flags", 2}});
	up.histories.at("beta").copy = true;
	r::MirrorTable table{7, up.layouts()};
	const auto bytes = r::encode_mirror_table(table);
	check(r::decode_mirror_table(bytes) == table);
	check(r::encode_mirror_table(r::decode_mirror_table(bytes)) == bytes);
	for (std::size_t size = 0; size < bytes.size(); ++size)
		refuses<r::MirrorRefused>([&] { r::decode_mirror_table(std::string_view(bytes).substr(0, size)); });
	for (std::size_t at = 0; at < bytes.size(); ++at) {
		auto flipped = bytes;
		flipped[at] ^= 0x10;
		refuses<r::MirrorRefused>([&] { r::decode_mirror_table(flipped); });
	}
	// Tiers: an optional base at the cut, contiguous blocks and sections, a copy at s and last, unique names.
	auto layout = up.layout("app/flags");
	r::check_layout(layout);
	auto baseless = layout;
	baseless.files.erase(baseless.files.begin()); // the namespace was empty at the cut
	r::check_layout(baseless);
	r::NamespaceLayout empty{"empty", identity('e'), 3, 3, 5, "policy-1", {}};
	r::check_layout(empty);
	check(empty.end() == 5);
	r::MirrorTable sparse{8, {baseless, empty}};
	check(r::decode_mirror_table(r::encode_mirror_table(sparse)) == sparse);
	auto broken = [&](auto edit) {
		auto copy = layout;
		edit(copy);
		refuses<std::invalid_argument>([&] { r::check_layout(copy); });
	};
	broken([](r::NamespaceLayout &l) { l.files[0].last += 1; });
	broken([](r::NamespaceLayout &l) { l.cut += 1; });
	broken([](r::NamespaceLayout &l) { l.files.push_back(l.files[0]); });
	broken([](r::NamespaceLayout &l) { l.files[1].first += 1; });
	broken([](r::NamespaceLayout &l) { std::swap(l.files[1], l.files[2]); });
	broken([](r::NamespaceLayout &l) { l.files.push_back(l.files[1]); });
	broken([](r::NamespaceLayout &l) { l.files[2].name = l.files[1].name; });
	broken([](r::NamespaceLayout &l) { l.files.push_back(file(K::Copy, "copy", 0, l.end() - 1)); });
	broken([](r::NamespaceLayout &l) {
		l.files.push_back(file(K::Copy, "copy", 0, l.end()));
		l.files.push_back(file(K::Section, "late", l.end() + 1, l.end() + 1));
	});
	broken([](r::NamespaceLayout &l) { l.name.clear(); });
	broken([](r::NamespaceLayout &l) { l.files[0].first = 1; });
	// The mirror's names are flat and fixed.
	check(r::mirror_file_name("app/flags") == r::mirror_file_name("app/flags") &&
		  r::mirror_file_name("app/flags").size() == 34 && r::mirror_file_name("app/flags").find('/') == std::string::npos);
}

// s is the newest tier's last position, or the cut when there is none.
void tiers() {
	Upstream up;
	auto &h = up.add("n");
	h.cut = 10;
	check(up.layout("n").end() == 10);
	up.advance({{"n", 5}}); // [11, 15]
	up.roll_up("n");
	up.advance({{"n", 3}}); // [16, 18]
	up.roll_up("n");
	up.advance({{"n", 2}}); // section [19, 20]
	up.advance({{"n", 1}}); // section [21, 21]
	auto layout = up.layout("n");
	check(layout.cut == 10 && layout.end() == 21);
	up.histories.at("n").copy = true;
	layout = up.layout("n");
	check(layout.end() == 21 && layout.files.back().kind == K::Copy);
}

// The upstream's script: seals, roll-ups, squashes, cuts, a policy change, a cached copy, a new
// incarnation and a namespace dropped and another added.
struct Script {
	Upstream up;
	std::size_t step = 0;
	Script() {
		up.add("app/flags");
		up.add("beta");
		up.add("gamma");
	}
	// Moves the upstream one step; returns the namespaces dropped by it.
	std::vector<std::string> next() {
		const auto i = step++;
		std::vector<std::string> dropped;
		up.advance({{"app/flags", 1 + i % 3}, {"beta", 2}});
		if (i % 4 == 3) {
			up.roll_up("app/flags");
			up.roll_up("beta");
		}
		auto &flags = up.histories.at("app/flags");
		if (i % 8 == 7 && flags.blocks.size() >= 3)
			up.squash("app/flags");
		if (i % 12 == 11 && flags.blocks.size() >= 2)
			up.cut("app/flags");
		auto &beta = up.histories.at("beta");
		if (i % 6 == 5 && beta.blocks.size() >= 2)
			up.cut("beta");
		if (i == 9)
			up.policy = "policy-2";
		if (i == 10)
			up.histories.at("beta").copy = true;
		if (i == 13)
			up.reincarnate("beta", 'b', 0);
		if (i == 15) {
			up.histories.erase("gamma");
			dropped.push_back("gamma");
			up.add("delta");
		}
		return dropped;
	}
};

// A4: the mirror follows the upstream's cut, squashes, policy and incarnations, fetching exactly the
// names it lacks and collecting the ones no longer reached.
void follows(const std::filesystem::path &root) {
	prepare(root);
	Script script;
	r::MirrorStore store(io_at(root), {});
	auto fetch = script.up.fetch();
	bool cut_seen = false, squash_seen = false;
	for (int i = 0; i < 24; ++i) {
		const auto before = store.table();
		const auto dropped = script.next();
		const auto layouts = script.up.layouts();
		std::uint64_t files = 0;
		const auto expected = bytes_of_new(before, layouts, files);
		const auto fetched = script.up.fetched_bytes;
		const auto stats = store.adopt(layouts, dropped, fetch);
		check(script.up.fetched_bytes - fetched == expected && stats.fetched_bytes == expected && stats.fetched_files == files);
		check(store.table().entries == layouts && store.table().sequence == before.sequence + 1);
		check(mirror_matches(root, store.table()));
		for (const auto &e : store.table().entries)
			check(e.policy == script.up.policy && e.seal == script.up.seal);
		for (const auto &e : before.entries)
			if (const auto *now = store.table().find(e.name)) {
				cut_seen = cut_seen || now->cut > e.cut;
				squash_seen = squash_seen || now->files.size() < e.files.size();
			}
		for (const auto &e : store.table().entries)
			check(e.cut == script.up.histories.at(e.name).cut);
	}
	check(cut_seen && squash_seen && !store.table().find("gamma") && store.table().find("delta"));
	check(store.table().find("beta")->incarnation == identity('b'));
}

// A2, the store half: a restart reads every file unchanged from disk and fetches nothing it holds.
void restart(const std::filesystem::path &root) {
	prepare(root);
	Script script;
	auto fetch = script.up.fetch();
	std::vector<std::string> before;
	{
		r::MirrorStore store(io_at(root), {});
		for (int i = 0; i < 20; ++i) {
			const auto dropped = script.next();
			store.adopt(script.up.layouts(), dropped, fetch);
		}
		before = reads(store, fetch);
	}
	script.up.reachable = false;
	const auto fetched = script.up.fetched_bytes;
	{
		r::MirrorStore store(io_at(root), {});
		check(store.failed().empty() && mirror_matches(root, store.table()));
		check(reads(store, fetch) == before);
		// The same layout again: every file reused, nothing fetched.
		const auto stats = store.adopt(script.up.layouts(), {}, fetch);
		check(stats.fetched_files == 0 && stats.reused_files > 0 && stats.collected_files == 0);
		check(script.up.fetched_bytes == fetched);
		std::fprintf(stderr, "restart: %zu files read unchanged, 0 bytes fetched, %llu files reused\n",
					 before.size(), static_cast<unsigned long long>(stats.reused_files));
		// The upstream's next seal: exactly the new names are fetched.
		script.up.reachable = true;
		const auto table = store.table();
		script.next();
		const auto layouts = script.up.layouts();
		std::uint64_t files = 0;
		const auto expected = bytes_of_new(table, layouts, files);
		const auto stats2 = store.adopt(layouts, {}, fetch);
		check(stats2.fetched_bytes == expected && script.up.fetched_bytes - fetched == expected);
	}
}

// D6 Recovery on the relay: a missing file fails its namespace until adopted again, which fetches that
// file alone; a rotted file is fetched again by name at its first read.
void repair(const std::filesystem::path &root) {
	prepare(root);
	Script script;
	auto fetch = script.up.fetch();
	{
		r::MirrorStore store(io_at(root), {});
		for (int i = 0; i < 6; ++i) {
			const auto dropped = script.next();
			store.adopt(script.up.layouts(), dropped, fetch);
		}
	}
	const auto table = r::decode_mirror_table(slurp(root / "mirror"));
	const auto &flags = *table.find("app/flags");
	const auto &rotted = flags.files[1];
	const auto &lost = table.find("beta")->files.back();
	auto bytes = slurp(root / r::mirror_file_name(rotted.name));
	bytes[bytes.size() / 2] ^= 1;
	write_file(root / r::mirror_file_name(rotted.name), bytes);
	std::filesystem::remove(root / r::mirror_file_name(lost.name));
	write_file(root / ".tmp-leftover", "x");
	write_file(root / "m-orphan", "y");
	r::MirrorStore store(io_at(root), {});
	check(store.failed().size() == 1 && store.failed()[0] == "beta");
	check(!std::filesystem::exists(root / ".tmp-leftover") && !std::filesystem::exists(root / "m-orphan"));
	auto files = script.up.fetched_files;
	check(store.read(rotted, fetch) == bytes_of(rotted.name) && script.up.fetched_files == files + 1);
	check(slurp(root / r::mirror_file_name(rotted.name)) == bytes_of(rotted.name));
	check(store.read(rotted, fetch) == bytes_of(rotted.name) && script.up.fetched_files == files + 1);
	files = script.up.fetched_files;
	const auto stats = store.adopt(script.up.layouts(), {}, fetch);
	check(stats.fetched_files == 1 && stats.fetched_bytes == lost.size && script.up.fetched_files == files + 1);
	check(store.failed().empty() && mirror_matches(root, store.table()));
}

// A file missing at startup that the next layout no longer reaches is collected without error.
void collect_missing(const std::filesystem::path &root) {
	prepare(root);
	Script script;
	auto fetch = script.up.fetch();
	{
		r::MirrorStore store(io_at(root), {});
		const auto dropped = script.next();
		store.adopt(script.up.layouts(), dropped, fetch);
	}
	const auto table = r::decode_mirror_table(slurp(root / "mirror"));
	std::filesystem::remove(root / r::mirror_file_name(table.find("gamma")->files.back().name));
	r::MirrorStore store(io_at(root), {});
	check(store.failed().size() == 1 && store.failed()[0] == "gamma");
	std::vector<std::string> dropped{"gamma"};
	const auto stats = store.adopt({}, dropped, fetch);
	check(stats.fetched_files == 0 && store.failed().empty() && !store.table().find("gamma"));
	check(mirror_matches(root, store.table()));
}

// A refused or failed fetch publishes nothing and leaves the mirror usable.
void refused(const std::filesystem::path &root) {
	prepare(root);
	Script script;
	auto fetch = script.up.fetch();
	r::MirrorStore store(io_at(root), {});
	const auto dropped = script.next();
	store.adopt(script.up.layouts(), dropped, fetch);
	const auto table = store.table();
	script.next();
	const auto layouts = script.up.layouts();
	refuses<r::FetchRefused>([&] {
		store.adopt(layouts, {}, [](const r::MirrorFile &f) { return bytes_of(f.name) + "!"; });
	});
	check(store.table() == table && mirror_matches(root, table));
	refuses<std::runtime_error>([&] {
		store.adopt(layouts, {}, [](const r::MirrorFile &) -> std::string { throw std::runtime_error("down"); });
	});
	check(store.table() == table && mirror_matches(root, table));
	// An upstream that changes an immutable name's bytes is refused before anything is fetched.
	auto changed = table.entries;
	changed[0].files[0].digest[0] ^= 1;
	refuses<r::FetchRefused>([&] { store.adopt(changed, {}, fetch); });
	check(store.table() == table);
	store.adopt(layouts, {}, fetch);
	check(store.table().entries == layouts && mirror_matches(root, store.table()));
	// One writer per directory.
	refuses<std::exception>([&] { r::MirrorStore second(io_at(root), {}); });
}

// A1, the relay half: a chain authority -> A -> B -> C. Each hop fetches from the one before by name,
// through that relay's own reads, and holds the same bytes under every name at every step.
void chain(const std::filesystem::path &scratch) {
	const auto ra = scratch / "relay-a", rb = scratch / "relay-b", rc = scratch / "relay-c";
	prepare(ra);
	prepare(rb);
	prepare(rc);
	Script script;
	auto authority = script.up.fetch();
	r::MirrorStore a(io_at(ra), {}), b(io_at(rb), {}), c(io_at(rc), {});
	std::uint64_t from_a = 0, from_b = 0;
	auto via = [](r::MirrorStore &store, const r::MirrorFetch &upstream, std::uint64_t &served) -> r::MirrorFetch {
		return [&store, upstream, &served](const r::MirrorFile &f) {
			auto bytes = store.read(f, upstream);
			served += bytes.size();
			return bytes;
		};
	};
	const auto fetch_b = via(a, authority, from_a), fetch_c = via(b, fetch_b, from_b);
	std::uint64_t compared = 0;
	for (int i = 0; i < 24; ++i) {
		const auto dropped = script.next();
		const auto layouts = script.up.layouts();
		a.adopt(layouts, dropped, authority);
		const auto fetched = script.up.fetched_bytes;
		b.adopt(a.table().entries, dropped, fetch_b);
		c.adopt(b.table().entries, dropped, fetch_c);
		check(script.up.fetched_bytes == fetched); // B and C never reach past their upstream
		check(a.table().entries == layouts && b.table().entries == layouts && c.table().entries == layouts);
		for (const auto &e : layouts)
			for (const auto &f : e.files) {
				const auto name = r::mirror_file_name(f.name);
				const auto bytes = slurp(ra / name);
				check(bytes == bytes_of(f.name) && slurp(rb / name) == bytes && slurp(rc / name) == bytes);
				++compared;
			}
		check(listing(ra) == listing(rb) && listing(rb) == listing(rc));
	}
	std::fprintf(stderr, "chain: 3 hops, %llu file comparisons byte-identical, %llu bytes from A, %llu from B\n",
				 static_cast<unsigned long long>(compared), static_cast<unsigned long long>(from_a),
				 static_cast<unsigned long long>(from_b));
}
} // namespace

int main(int argc, char **argv) {
	check(argc == 2);
	const std::filesystem::path scratch = argv[1];
	std::filesystem::remove_all(scratch);
	std::filesystem::create_directories(scratch);
	try {
		codec();
		tiers();
		follows(scratch / "follows");
		restart(scratch / "restart");
		repair(scratch / "repair");
		collect_missing(scratch / "collect-missing");
		refused(scratch / "refused");
		chain(scratch);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_relay_mirror: %s\n", error.what());
		return 1;
	}
	std::filesystem::remove_all(scratch);
	std::fprintf(stderr, "test_relay_mirror: ok\n");
	return 0;
}
