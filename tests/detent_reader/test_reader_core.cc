// The reader core's C ABI and C++ header (reader.md D1, D2, D4, D7; docs/mechanisms/reader-core.md).
#include "detent/reader.hh"
#include "detent_local/agent_store.hh"
#include "detent_testing/generation_writer.hh"
#include "generation_file_access.hh"
#include "prepared_reader.hh"
#include "testing.hh"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// UBSan's vtable check on Linux reads through a pipe, so it aborts once descriptors run out.
extern "C" [[gnu::weak]] void __ubsan_handle_dynamic_type_cache_miss(void *, std::uintptr_t, std::uintptr_t);
namespace x = detent::local::detail;
namespace t = detent::reader::testing;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("reader core invariant at line " + std::to_string(line));
}
#define CHECK(value) check(value, __LINE__)
detent_str S(std::string_view s) { return {s.data(), s.size()}; }
constexpr detent_str none{nullptr, 0};
std::string knobs(std::int64_t rev) {
	return R"({"schema_version":1,"knobs":{)"
	       R"("t:rev":{"default":)" + std::to_string(rev) + R"(},)"
	       R"("t:min":{"default":-9223372036854775808},)"
	       R"("t:max":{"default":9223372036854775807},)"
	       R"("t:flag":{"default":0,"overrides":{"vip":1,"half":{"rollout_pct":50}},"rollout_value":1},)"
	       R"("t:on":{"default":1},)"
	       R"("t:size":{"default":25,"overrides":{"big":{"rollout_pct":40}},"rollout_pct":10,"rollout_value":50},)"
	       R"("u:ramp":{"default":0,"rollout_pct":30,"rollout_value":1},)"
	       R"("u:all":{"default":0,"rollout_pct":100,"rollout_value":1}}})";
}
struct Opened {
	detent_reader *handle = nullptr;
	detent_status status;
	Opened(const std::filesystem::path &store, std::vector<detent_str> namespaces = {}, uid_t uid = ::getuid(),
	       std::uint64_t mapping_bytes = 0, std::size_t size = sizeof(detent_open_options)) {
		const auto path = store.string();
		detent_open_options options{size, S(path), namespaces.data(), namespaces.size(), uint32_t(uid), mapping_bytes};
		status = detent_open(&options, &handle);
	}
	~Opened() { detent_close(handle); }
};
std::int64_t get(detent_reader *h, std::string_view name, detent_status expect, std::int64_t fallback = -7,
                 detent_str hash = none, detent_str switch_value = none) {
	std::int64_t out = 0;
	const auto status = detent_get(h, S(name), hash, switch_value, fallback, &out);
	if (status != expect)
		throw std::runtime_error(std::string(name) + ": status " + detent_status_name(status) + ", expected " +
		                         detent_status_name(expect));
	return out;
}
void replaced_selected(const std::filesystem::path &root) {
	const auto store = root / "replaced-selected";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	x::RecordTesting::install([](std::string_view name, void *context) {
		if (name != "selected")
			return;
		x::RecordTesting::install(nullptr, nullptr);
		auto &writer = *static_cast<detent::testing::GenerationWriter *>(context);
		CHECK(writer.publish_json(knobs(2)) == 2);
	}, &writer);
	struct Reset {
		~Reset() { x::RecordTesting::install(nullptr, nullptr); }
	} reset;
	Opened r(store);
	CHECK(r.status == DETENT_OK);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 2);
}
// `selected` is read once per table: at open, which the first lookup attaches from while the store
// word holds (D4), and once per switch, which attaches from that read.
void selected_reads(const std::filesystem::path &root) {
	const auto store = root / "selected-reads";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	unsigned reads = 0;
	x::RecordTesting::install([](std::string_view name, void *context) {
		if (name == "selected")
			++*static_cast<unsigned *>(context);
	}, &reads);
	struct Reset {
		~Reset() { x::RecordTesting::install(nullptr, nullptr); }
	} reset;
	Opened r(store);
	CHECK(r.status == DETENT_OK && reads == 1);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1 && reads == 1);
	CHECK(writer.publish_json(knobs(2)) == 2);
	reads = 0;
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2 && reads == 1);
	// A publish between the open and the first lookup moves the store word: the lookup reads again.
	Opened late(store);
	CHECK(late.status == DETENT_OK && writer.publish_json(knobs(3)) == 3);
	reads = 0;
	CHECK(get(late.handle, "t:rev", DETENT_OK) == 3 && reads == 1);
}
// The agent's table: an entry per namespace (reader.md D2, A1-A2). A lookup attaches its namespace's
// entry alone, follows that entry's sequence, and a publish of another namespace refreshes nothing.
// The agent adds a namespace: `change` covers its slot before `selected` names it (D4 rule 4), so an
// open, a refresh and a first lookup between the table's rename and the store word's bump read it.
void adding_namespace(const std::filesystem::path &root) {
	static struct {
		bool armed = false;
		std::filesystem::path store;
		detent_reader *handle = nullptr;
		detent_status opened = DETENT_OK, refreshed = DETENT_OK, looked = DETENT_OK;
		std::int64_t a = 0, b = 0;
	} w;
	w.store = root / "adding";
	detent::testing::GenerationWriter writer(w.store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"a:k":{"default":1}}})") == 1);
	Opened r(w.store);
	CHECK(r.status == DETENT_OK && get(r.handle, "a:k", DETENT_OK) == 1);
	w.handle = r.handle;
	w.armed = true;
	detent::local::AgentStoreTesting::install(
	    [](void *, const char *what) noexcept {
		    if (!w.armed || std::string_view(what) != "rename table")
			    return;
		    w.armed = false;
		    Opened other(w.store);
		    w.opened = other.status;
		    w.refreshed = detent_refresh(w.handle);
		    w.looked = detent_get(w.handle, S("b:k"), none, none, -7, &w.b);
		    (void)detent_get(w.handle, S("a:k"), none, none, -7, &w.a);
	    },
	    nullptr);
	CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"a:k":{"default":2},"b:k":{"default":3}}})") == 2);
	detent::local::AgentStoreTesting::install(nullptr, nullptr);
	CHECK(!w.armed && w.opened == DETENT_OK && w.refreshed == DETENT_OK);
	CHECK(w.looked == DETENT_OK && w.b == 3 && w.a == 2);
	CHECK(get(r.handle, "a:k", DETENT_OK) == 2 && get(r.handle, "b:k", DETENT_OK) == 3);
}
// A namespace whose next generation cannot attach fails alone (D4): it answers its last generation and
// the handle's other namespaces follow their publishes; detent_refresh reports the failure.
void entry_failure(const std::filesystem::path &root) {
	const auto store = root / "entry-failure";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"t:k":{"default":1},"u:k":{"default":1}}})") == 1);
	Opened both(store), only_t(store, {S("t")});
	CHECK(get(both.handle, "t:k", DETENT_OK) == 1 && get(both.handle, "u:k", DETENT_OK) == 1);
	CHECK(get(only_t.handle, "t:k", DETENT_OK) == 1);
	CHECK(writer.publish_delta({"t", "u"}, "u", {{"u:k", 2}}, {}) == 2);
	const auto u2 = x::PreparedReader::selection(store, ::getuid(), "u").descriptor.generation;
	const auto metadata = store / (detent::local::generation_name(u2) + ".read");
	std::filesystem::rename(metadata, root / "entry-failure.read");
	for (const std::int64_t v : {3, 4}) {
		CHECK(writer.publish_delta({"t", "u"}, "t", {{"t:k", v}}, {}) == std::uint64_t(v));
		CHECK(get(both.handle, "t:k", DETENT_OK) == v && get(only_t.handle, "t:k", DETENT_OK) == v);
		CHECK(get(both.handle, "u:k", DETENT_OK) == 1); // its last generation, without retrying
	}
	CHECK(detent_refresh(both.handle) == DETENT_UNAVAILABLE && get(both.handle, "t:k", DETENT_OK) == 4);
	std::filesystem::rename(root / "entry-failure.read", metadata);
	CHECK(get(both.handle, "u:k", DETENT_OK) == 1); // remembered at the sequence it failed at
	CHECK(detent_refresh(both.handle) == DETENT_OK && get(both.handle, "u:k", DETENT_OK) == 2);
	CHECK(writer.publish_delta({"t", "u"}, "t", {{"t:k", 5}}, {}) == 5);
	CHECK(get(both.handle, "t:k", DETENT_OK) == 5 && get(both.handle, "u:k", DETENT_OK) == 2);
}
void per_namespace(const std::filesystem::path &root) {
	const auto store = root / "per-namespace";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"t:rev":{"default":1},"u:all":{"default":1}}})") == 1);
	CHECK(detent::local::read_generation_selections(store).size() == 2); // entries t and u
	Opened all(store), only_u(store, {S("u")});
	CHECK(all.status == DETENT_OK && only_u.status == DETENT_OK);
	CHECK(get(all.handle, "t:rev", DETENT_OK) == 1 && t::stats(all.handle).attached == 1);
	CHECK(get(all.handle, "u:all", DETENT_OK) == 1 && t::stats(all.handle).attached == 2);
	CHECK(get(only_u.handle, "u:all", DETENT_OK) == 1 && get(only_u.handle, "t:rev", DETENT_NOT_OPENED) == -7);
	// A namespace the table does not list has no generation yet; its first publish attaches it.
	CHECK(get(all.handle, "v:x", DETENT_UNAVAILABLE) == -7);
	// A delta to `u` moves u's entry alone: t answers without a refresh, at its own revision.
	CHECK(writer.publish_delta({"t", "u"}, "u", {{"u:all", 5}}, {}) == 2);
	const auto before = t::stats(all.handle).publishes;
	CHECK(get(all.handle, "t:rev", DETENT_OK) == 1 && t::stats(all.handle).publishes == before);
	CHECK(get(all.handle, "u:all", DETENT_OK) == 5 && t::stats(all.handle).publishes == before + 1);
	std::uint64_t revision = 0;
	CHECK(detent_revision(all.handle, S("t"), &revision) == DETENT_OK && revision == 1);
	CHECK(detent_revision(all.handle, S("u"), &revision) == DETENT_OK && revision == 2);
	CHECK(get(only_u.handle, "u:all", DETENT_OK) == 5);
	CHECK(writer.publish_json(R"({"schema_version":1,"knobs":{"t:rev":{"default":3},"v:x":{"default":4}}})") == 3);
	CHECK(get(all.handle, "v:x", DETENT_OK) == 4 && get(all.handle, "t:rev", DETENT_OK) == 3);
	// A namespace that left the table keeps answering from its last generation; a new handle has none.
	CHECK(get(all.handle, "u:all", DETENT_OK) == 5);
	Opened later(store);
	CHECK(get(later.handle, "u:all", DETENT_UNAVAILABLE) == -7 && get(later.handle, "v:x", DETENT_OK) == 4);
	// Many namespaces: the open reads `selected` once, and the first lookup attaches one entry from it.
	const auto wide = root / "per-namespace-wide";
	detent::testing::GenerationWriter many(wide, detent::testing::GenerationWriter::Layout::PerNamespace);
	std::string json = R"({"schema_version":1,"knobs":{)";
	for (int n = 0; n != 64; ++n)
		json += (n ? "," : "") + std::string("\"n") + std::to_string(n) + R"(:k":{"default":)" + std::to_string(n) + "}";
	CHECK(many.publish_json(json + "}}") == 1);
	unsigned reads = 0;
	x::RecordTesting::install([](std::string_view name, void *context) {
		if (name == "selected")
			++*static_cast<unsigned *>(context);
	}, &reads);
	struct Reset {
		~Reset() { x::RecordTesting::install(nullptr, nullptr); }
	} reset;
	Opened r(wide);
	CHECK(r.status == DETENT_OK && get(r.handle, "n37:k", DETENT_OK) == 37);
	CHECK(reads == 1 && t::stats(r.handle).attached == 1);
	for (int n = 0; n != 64; ++n)
		CHECK(get(r.handle, "n" + std::to_string(n) + ":k", DETENT_OK) == n);
	CHECK(reads == 1 && t::stats(r.handle).attached == 64 && t::stats(r.handle).live_views <= 4);
}
// A switch that cannot read the table, as when a publish storm outruns `selected`'s retries, keeps
// the attached generation answering, stale (D4), until a read succeeds.
void unreadable_table(const std::filesystem::path &root) {
	const auto store = root / "unreadable-table";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	Opened r(store);
	CHECK(r.status == DETENT_OK && get(r.handle, "t:rev", DETENT_OK) == 1);
	CHECK(writer.publish_json(knobs(2)) == 2);
	std::filesystem::rename(store / "selected", store / "held-selected");
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 1);
	CHECK(detent_refresh(r.handle) == DETENT_UNAVAILABLE && get(r.handle, "t:rev", DETENT_OK) == 1);
	std::filesystem::rename(store / "held-selected", store / "selected");
	CHECK(detent_refresh(r.handle) == DETENT_OK && get(r.handle, "t:rev", DETENT_OK) == 2);
}
// A failure remembered by a namespace's first attach lasts until the store word moves or a
// detent_refresh that does not fail (D4), then the next lookup attaches.
void refresh_memo(const std::filesystem::path &root) {
	const auto store = root / "refresh-memo";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	Opened r(store);
	CHECK(r.status == DETENT_OK);
	const auto selected = x::PreparedReader::selection(store, ::getuid(), "t").descriptor.generation;
	const auto metadata = store / (detent::local::generation_name(selected) + ".read");
	std::filesystem::rename(metadata, root / "memo-hidden.read");
	CHECK(get(r.handle, "t:rev", DETENT_UNAVAILABLE) == -7);
	CHECK(get(r.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // remembered
	std::filesystem::rename(root / "memo-hidden.read", metadata);
	CHECK(get(r.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // still remembered: the word holds
	CHECK(detent_refresh(r.handle) == DETENT_OK);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1 && t::stats(r.handle).attached == 1);
}
// A store of two packs: the first lookup maps one, and some key lives only in the other.
constexpr int wide_knobs = 60000;
std::string wide_name(int i) { return "t:k" + std::to_string(i); }
std::string wide_json(int rev) {
	std::string json = R"({"schema_version":1,"knobs":{)";
	for (int i = 0; i != wide_knobs; ++i)
		json += (i ? ",\"" : "\"") + wide_name(i) + R"(":{"default":)" + std::to_string(3 * i + rev) + "}";
	return json + "}}";
}
std::filesystem::path wide_store(const std::filesystem::path &root) {
	const auto store = root / "wide";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(wide_json(1)) == 1);
	return store;
}
// How many keys of revision `rev` answer, each with its value.
int wide_reads(detent_reader *h, int rev) {
	int ok = 0;
	for (int i = 0; i != wide_knobs; ++i) {
		std::int64_t out = 0;
		const auto name = wide_name(i);
		const auto status = detent_get(h, S(name), none, none, -7, &out);
		CHECK(status == DETENT_OK ? out == 3 * i + rev : status == DETENT_NO_MEMORY && out == -7);
		ok += status == DETENT_OK;
	}
	return ok;
}
// The first key past k0 whose lookup does not answer, and its status.
std::pair<int, detent_status> first_refused(detent_reader *h) {
	for (int i = 1; i != wide_knobs; ++i) {
		std::int64_t out = 0;
		const auto name = wide_name(i);
		if (const auto status = detent_get(h, S(name), none, none, -7, &out); status != DETENT_OK) {
			CHECK(out == -7);
			return {i, status};
		}
		CHECK(out == 3 * i + 1);
	}
	return {-1, DETENT_OK};
}
// A cold miss that fails for want of a descriptor is DETENT_IO for that lookup alone: the generation
// keeps answering, and the same key answers once descriptors return.
void transient_miss(const std::filesystem::path &store) {
#ifdef __linux__
	if (&__ubsan_handle_dynamic_type_cache_miss) {
		std::cout << "transient_miss: skipped, UBSan's vtable check needs descriptors\n";
		return;
	}
#endif
	Opened r(store);
	CHECK(r.status == DETENT_OK && get(r.handle, "t:k0", DETENT_OK) == 1);
	rlimit saved{};
	CHECK(::getrlimit(RLIMIT_NOFILE, &saved) == 0);
	auto lowered = saved;
	lowered.rlim_cur = std::min<rlim_t>(saved.rlim_cur, 256);
	CHECK(::setrlimit(RLIMIT_NOFILE, &lowered) == 0);
	std::vector<int> held{::open("/dev/null", O_RDONLY)};
	CHECK(held.front() >= 0);
	for (int fd; (fd = ::dup(held.front())) >= 0;)
		held.push_back(fd);
	const auto [cold, status] = first_refused(r.handle);
	for (const int fd : held)
		::close(fd);
	CHECK(::setrlimit(RLIMIT_NOFILE, &saved) == 0);
	CHECK(cold > 0 && status == DETENT_IO);
	CHECK(get(r.handle, "t:k0", DETENT_OK) == 1);
	CHECK(get(r.handle, wide_name(cold), DETENT_OK) == 3 * cold + 1);
	CHECK(first_refused(r.handle).first < 0 && detent_refresh(r.handle) == DETENT_OK);
}
// The mapping cap (detent_open_options): 512 MiB by default, also for options without the field;
// below one pack and its bitmap the open fails. Past the cap a cold miss is refused with the fallback
// and the mapped segments keep answering; a raised cap reads every key.
void capped(const std::filesystem::path &store) {
	constexpr std::uint64_t minimum = DETENT_MINIMUM_MAPPING_BYTES;
	const auto uid = ::getuid();
	CHECK(t::stats(Opened(store).handle).mapped_limit == 512u << 20);
	const Opened old(store, {}, uid, minimum, offsetof(detent_open_options, mapping_bytes));
	CHECK(old.status == DETENT_OK && t::stats(old.handle).mapped_limit == 512u << 20);
	CHECK(Opened(store, {}, uid, minimum - 1).status == DETENT_INVALID_ARGUMENT);
	Opened mapped(store, {}, uid, minimum);
	CHECK(mapped.status == DETENT_OK && t::stats(mapped.handle).mapped_limit == minimum);
	CHECK(get(mapped.handle, "t:k0", DETENT_OK) == 1);
	const auto [cold, status] = first_refused(mapped.handle);
	const auto charged = t::stats(mapped.handle).mapped_bytes;
	std::cout << "mapping cap " << minimum << ": " << wide_name(cold) << " " << detent_status_name(status)
	          << ", charged " << charged << "\n";
	CHECK(cold > 0 && status == DETENT_NO_MEMORY && charged > 0 && charged <= minimum);
	CHECK(get(mapped.handle, "t:k0", DETENT_OK) == 1);
	Opened raised(store, {}, uid, std::uint64_t(1) << 40);
	CHECK(raised.status == DETENT_OK && t::stats(raised.handle).mapped_limit == std::uint64_t(1) << 40);
	CHECK(get(raised.handle, "t:k0", DETENT_OK) == 1 && first_refused(raised.handle).first < 0);
	std::cout << "raised cap: every key, charged " << t::stats(raised.handle).mapped_bytes << "\n";
}
// A view a lookup pinned across a commit is freed by the next refresh once the pin drops, Unchanged
// or not, and returns its mapping charge: with room for one generation, the new one reads whole.
void retained(const std::filesystem::path &root) {
	const auto store = root / "retained";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(wide_json(1)) == 1);
	std::uint64_t whole = 0;
	{
		Opened wide(store, {}, ::getuid(), std::uint64_t(1) << 40);
		CHECK(wide_reads(wide.handle, 1) == wide_knobs);
		whole = t::stats(wide.handle).mapped_bytes;
	}
	Opened r(store, {}, ::getuid(), whole + whole / 2);
	CHECK(r.status == DETENT_OK && wide_reads(r.handle, 1) == wide_knobs);
	struct Park {
		std::atomic<int> state{0}; // 1 parked, 2 released
	} park;
	static thread_local Park *armed = nullptr;
	t::install([](t::Window at, void *) noexcept {
		if (at != t::Window::Pinned || !armed)
			return;
		auto &park = *std::exchange(armed, nullptr);
		park.state = 1;
		while (park.state.load() != 2)
			std::this_thread::yield();
	}, nullptr);
	std::int64_t seen = 0;
	std::thread reader([&] {
		armed = &park;
		detent_get(r.handle, S("t:k0"), none, none, -7, &seen);
	});
	while (park.state.load() != 1)
		std::this_thread::yield();
	CHECK(writer.publish_json(wide_json(2)) == 2 && detent_refresh(r.handle) == DETENT_OK);
	CHECK(t::stats(r.handle).generations == 2); // the pinned view keeps revision 1
	park.state = 2;
	reader.join();
	t::install(nullptr, nullptr);
	CHECK(seen == 2 && detent_refresh(r.handle) == DETENT_OK);
	const int ok = wide_reads(r.handle, 2);
	const auto stats = t::stats(r.handle);
	std::cout << "retained: revision 2 reads " << ok << " of " << wide_knobs << ", generations " << stats.generations
	          << ", charged " << stats.mapped_bytes << " of " << stats.mapped_limit << "\n";
	CHECK(ok == wide_knobs && stats.generations == 1 && stats.live_views == 1);
}
// A refused fork-handler registration fails the open; the next open registers them.
void atfork_refused(const std::filesystem::path &root) {
	const auto store = root / "atfork";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	t::refuse_atfork(true);
	{
		Opened refused(store);
		CHECK(refused.status == DETENT_NO_MEMORY && refused.handle == nullptr);
	}
	t::refuse_atfork(false);
	Opened r(store);
	CHECK(r.status == DETENT_OK && get(r.handle, "t:rev", DETENT_OK) == 1);
}
// Once the fork handlers registered, an open takes no lock: a child forked while another thread is
// inside detent_open opens its own handle instead of waiting on a lock no thread of it holds.
void fork_mid_open(const std::filesystem::path &root) {
	const auto store = root / "fork-open";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	static std::atomic<int> state{0}; // 1 parked, 2 released
	static thread_local bool armed = false;
	t::install([](t::Window at, void *) noexcept {
		if (at != t::Window::Registering || !std::exchange(armed, false))
			return;
		state = 1;
		while (state.load() != 2)
			std::this_thread::yield();
	}, nullptr);
	detent_status opened = DETENT_INVALID_ARGUMENT;
	std::thread opener([&] {
		armed = true;
		Opened r(store);
		opened = r.status;
	});
	while (state.load() != 1)
		std::this_thread::yield();
	const pid_t pid = ::fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		::alarm(10); // the deadlock this checks: SIGALRM ends the child
		detent_reader *h = nullptr;
		const auto path = store.string();
		detent_open_options options{sizeof options, S(path), nullptr, 0, uint32_t(::getuid()), 0};
		std::int64_t out = 0;
		const bool ok = detent_open(&options, &h) == DETENT_OK &&
		                detent_get(h, S("t:rev"), none, none, -7, &out) == DETENT_OK && out == 1;
		std::_Exit(ok ? 0 : 1);
	}
	int status = 0;
	CHECK(::waitpid(pid, &status, 0) == pid);
	state = 2;
	opener.join();
	t::install(nullptr, nullptr);
	std::cout << "fork mid-open: child " << (WIFEXITED(status) ? "exit " : "signal ")
	          << (WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status)) << "\n";
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && opened == DETENT_OK);
}
void replaced_change(const std::filesystem::path &root) {
	const auto store = root / "replaced-change";
	{
		detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
		CHECK(writer.publish_json(knobs(1)) == 1);
	}
	struct Race {
		std::filesystem::path store;
		unsigned calls = 0;
	} race{store};
	t::install([](t::Window at, void *context) noexcept {
		if (at != t::Window::ChangeMapped)
			return;
		auto &race = *static_cast<Race *>(context);
		if (race.calls++ == 0) {
			const auto change = race.store / "change";
			const auto old = race.store / "old-change";
			std::filesystem::rename(change, old);
			std::filesystem::copy_file(old, change);
		}
	}, &race);
	struct Reset {
		~Reset() { t::install(nullptr, nullptr); }
	} reset;
	Opened r(store);
	CHECK(r.status == DETENT_OK && race.calls == 1);
	CHECK(detent_refresh(r.handle) == DETENT_OK && race.calls == 2);
	t::install(nullptr, nullptr);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1);
	// The next publisher writes inode B; a reader still mapped to A cannot see its sequence move.
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(2)) == 2);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 2);
}
void values(const std::filesystem::path &root) {
	const auto store = root / "values";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(1)) == 1);
	Opened r(store);
	CHECK(r.status == DETENT_OK);
	// Lazy attach (D2): open attaches nothing; the first lookup attaches the one table entry.
	CHECK(t::stats(r.handle).attached == 0 && t::stats(r.handle).generations == 0);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1);
	CHECK(t::stats(r.handle).attached == 1 && t::stats(r.handle).generations == 1);
	CHECK(get(r.handle, "t:min", DETENT_OK) == INT64_MIN && get(r.handle, "t:max", DETENT_OK) == INT64_MAX);
	CHECK(get(r.handle, "t:absent", DETENT_MISSING) == -7);
	CHECK(get(r.handle, "zz:absent", DETENT_UNAVAILABLE) == -7); // a namespace with no entry has no generation
	CHECK(get(r.handle, "t:flag", DETENT_OK) == 0);
	for (const auto bad : {"t", "t:", ":rev", "t:rev:x", "t rev", "t:r*v"})
		CHECK(get(r.handle, bad, DETENT_INVALID_NAME) == -7);
	std::int64_t out = 0;
	CHECK(detent_get(r.handle, none, none, none, 5, &out) == DETENT_INVALID_NAME && out == 5);
	CHECK(detent_get(r.handle, S("t:rev"), none, none, 5, nullptr) == DETENT_INVALID_ARGUMENT);
	CHECK(detent_get(nullptr, S("t:rev"), none, none, 5, &out) == DETENT_INVALID_ARGUMENT && out == 5);
	// One knob type: every knob takes the hash and switch alike (D1 item 3).
	CHECK(get(r.handle, "t:flag", DETENT_OK, -7, none, S("vip")) == 1);
	CHECK(get(r.handle, "t:on", DETENT_OK) == 1);
	CHECK(get(r.handle, "t:rev", DETENT_OK, -7, S("anyone"), S("vip")) == 1); // no override: default
	CHECK(get(r.handle, "t:nope", DETENT_MISSING, 3) == 3);
	CHECK(get(r.handle, "u:all", DETENT_OK, -7, S("anyone")) == 1);
	CHECK(get(r.handle, "u:all", DETENT_OK) == 0); // no hash: default
	CHECK(get(r.handle, "t:max", DETENT_OK, -7, S("x"), S("y")) == INT64_MAX);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 1);
	CHECK(detent_revision(r.handle, S("t:x"), &revision) == DETENT_INVALID_NAME && revision == 0);
	// Oracle: every knob across hashes and switches matches the prepared reader's view.
	auto t_oracle = x::PreparedReader::open(store, ::getuid(), {}, std::nullopt, "t");
	auto u_oracle = x::PreparedReader::open(store, ::getuid(), {}, std::nullopt, "u");
	std::size_t compared = 0;
	for (const auto name : {"t:flag", "t:on", "t:size", "t:rev", "u:ramp", "u:all"})
		for (int h = -1; h != 200; ++h)
			for (const auto sw : {"", "vip", "half", "big", "other"}) {
				const std::string hash = "user" + std::to_string(h);
				const std::string_view qualified(name);
				const auto colon = qualified.find(':');
				const auto &oracle = qualified.starts_with("t:") ? t_oracle : u_oracle;
				const auto expected =
				    oracle.view.get(qualified.substr(0, colon), qualified.substr(colon + 1),
				                    h < 0 ? std::nullopt : std::optional<std::string_view>(hash),
				                    *sw ? std::optional<std::string_view>(sw) : std::nullopt);
				CHECK(get(r.handle, name, DETENT_OK, ~expected.value(), h < 0 ? none : S(hash), *sw ? S(sw) : none) ==
				      expected.value());
				++compared;
			}
	CHECK(compared == 6 * 201 * 5);
	// A publish switches the next lookup through the namespace's sequence (D4).
	CHECK(writer.publish_json(knobs(2)) == 2);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2);
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 2);
	CHECK(t::stats(r.handle).generations == 2 && t::stats(r.handle).live_views <= 4); // t and u
	// detent_refresh pays the switch from the caller's thread; lookups then answer without switching.
	CHECK(writer.publish_json(knobs(3)) == 3);
	const auto before = t::stats(r.handle).publishes;
	CHECK(detent_refresh(r.handle) == DETENT_OK);
	CHECK(t::stats(r.handle).publishes == before + 1);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 3 && t::stats(r.handle).publishes == before + 1);
	CHECK(detent_refresh(r.handle) == DETENT_OK && t::stats(r.handle).publishes == before + 1); // unchanged
	// A namespace list: names outside it are refused before any lookup.
	Opened listed(store, {S("u")});
	CHECK(listed.status == DETENT_OK);
	CHECK(get(listed.handle, "t:rev", DETENT_NOT_OPENED) == -7);
	CHECK(get(listed.handle, "u:all", DETENT_OK, -7, S("x")) == 1);
	CHECK(detent_revision(listed.handle, S("t"), &revision) == DETENT_NOT_OPENED);
	// A failed switch keeps the installed view (D4): hide the new generation's reader metadata.
	CHECK(writer.publish_json(knobs(4)) == 4);
	const auto selected = x::PreparedReader::selection(store, ::getuid(), "t").descriptor.generation;
	const auto metadata = store / (detent::local::generation_name(selected) + ".read");
	std::filesystem::rename(metadata, root / "hidden.read");
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 3);
	CHECK(detent_refresh(r.handle) == DETENT_UNAVAILABLE);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 3);
	std::filesystem::rename(root / "hidden.read", metadata);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 3); // remembered until the store word moves or a refresh
	CHECK(detent_refresh(r.handle) == DETENT_OK);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 4);
	// A failure is remembered at the store word its attempt read (D4): a publish landing during the
	// attempt moves the word past the memo, so the lookup retries at once and answers the new generation.
	struct Race {
		detent::testing::GenerationWriter &writer;
		std::filesystem::path store, hidden;
		std::filesystem::path metadata{};
		int fired = 0;
	} race{writer, store, root / "raced.read"};
	auto selected_metadata = [](const std::filesystem::path &at) {
		const auto generation = x::PreparedReader::selection(at, ::getuid(), "t").descriptor.generation;
		return at / (detent::local::generation_name(generation) + ".read");
	};
	CHECK(writer.publish_json(knobs(5)) == 5);
	const auto fifth = selected_metadata(store);
	std::filesystem::rename(fifth, root / "fifth.read");
	t::install(
	    [](t::Window at, void *context) noexcept {
		    auto &race = *static_cast<Race *>(context);
		    if (at != t::Window::Attaching)
			    return;
		    if (race.fired++) {
			    // The retry: the word moved past the failed attempt; restore what it needs.
			    if (race.fired == 2)
				    std::filesystem::rename(race.hidden, race.metadata);
			    return;
		    }
		    try {
			    if (race.writer.publish_json(knobs(6)) != 6)
				    std::abort();
			    const auto generation = x::PreparedReader::selection(race.store, ::getuid(), "t").descriptor.generation;
			    race.metadata = race.store / (detent::local::generation_name(generation) + ".read");
			    std::filesystem::rename(race.metadata, race.hidden);
		    } catch (...) {
			    std::abort();
		    }
	    },
	    &race);
	// Attaches: t's failed attempt and u's fifth, which publishes alone; then t's and u's sixth.
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 6 && race.fired == 4);
	t::install(nullptr, nullptr);
	std::filesystem::remove(root / "fifth.read");
	CHECK(t::stats(r.handle).generations == 2);
}
void wrapper(const std::filesystem::path &root) {
	const auto store = root / "wrapper";
	detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
	CHECK(writer.publish_json(knobs(5)) == 1);
	detent::Reader reader(store.string(), ::getuid());
	CHECK(reader.get("t:rev", 0) == 5 && reader.get("t:absent", 9) == 9 && reader.get("bad", 8) == 8);
	CHECK(reader.get("t:flag", 7, std::nullopt, "vip") == 1 && reader.get("t:flag", 7) == 0);
	CHECK(reader.get_as<bool>("t:flag", false, std::nullopt, "vip") && !reader.get_as<bool>("t:flag", true));
	CHECK(reader.get_as<bool>("t:rev", true) && !reader.get_as<bool>("t:rev", false)); // 5 is not a flag
	CHECK(reader.get_as<std::int8_t>("t:max", 3) == 3 && reader.get_as<std::uint8_t>("t:rev", 0) == 5);
	CHECK(reader.get_as<std::uint64_t>("t:min", 4) == 4 && reader.get_as<std::int64_t>("t:min", 4) == INT64_MIN);
	auto found = reader.lookup("t:min", 0);
	CHECK(found && found.value == INT64_MIN);
	CHECK(reader.lookup("t:zz", 6).status == DETENT_MISSING && !reader.lookup("t:zz", 6) &&
	      reader.lookup("t:zz", 6).value == 6);
	CHECK(reader.revision("t") == 1u && !reader.revision("t:x"));
	detent::Reader moved(std::move(reader));
	CHECK(moved.get("t:rev", 0) == 5 && reader.native() == nullptr && reader.get("t:rev", 6) == 6);
	CHECK(reader.refresh() == DETENT_INVALID_ARGUMENT && moved.refresh() == DETENT_OK);
	bool threw = false;
	try {
		detent::Reader bad("", ::getuid());
	} catch (const detent::ReaderError &error) {
		threw = error.status() == DETENT_INVALID_ARGUMENT;
	}
	CHECK(threw);
}
void opens(const std::filesystem::path &root) {
	detent_reader *h = reinterpret_cast<detent_reader *>(1);
	CHECK(detent_abi_version() == DETENT_ABI_VERSION);
	CHECK(detent_open(nullptr, &h) == DETENT_INVALID_ARGUMENT && h == nullptr);
	const auto path = (root / "absent").string();
	detent_open_options options{sizeof options, S(path), nullptr, 0, uint32_t(::getuid())};
	CHECK(detent_open(&options, nullptr) == DETENT_INVALID_ARGUMENT);
	options.size = 8;
	CHECK(detent_open(&options, &h) == DETENT_INVALID_ARGUMENT);
	options.size = sizeof options;
	options.store = {nullptr, 0};
	CHECK(detent_open(&options, &h) == DETENT_INVALID_ARGUMENT);
	options.store = S(path);
	const detent_str bad_namespaces[] = {S("ok"), S("no:pe")};
	options.namespaces = bad_namespaces;
	options.namespace_count = 2;
	CHECK(detent_open(&options, &h) == DETENT_INVALID_ARGUMENT);
	options.namespace_count = 1;
	CHECK(std::string(detent_status_name(DETENT_UNTRUSTED)) == "untrusted" &&
	      std::string(detent_status_name(detent_status(15))) == "unknown");
	{
		// Q6 (a): a store that does not exist yet opens; lookups answer the fallback as unavailable
		// and retry the open at most once per 200 ms.
		Opened empty(root / "later");
		CHECK(empty.status == DETENT_OK);
		CHECK(get(empty.handle, "t:rev", DETENT_UNAVAILABLE) == -7);
		{
			// A complete store renamed into place just after a failed try waits for the next one.
			Opened moved(root / "moved");
			detent::testing::GenerationWriter staged(root / "staged", detent::testing::GenerationWriter::Layout::PerNamespace);
			CHECK(moved.status == DETENT_OK && staged.publish_json(knobs(8)) == 1);
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			CHECK(get(moved.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // tries: no directory
			std::filesystem::rename(root / "staged", root / "moved");
			CHECK(get(moved.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // within 200 ms of that try
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			CHECK(get(moved.handle, "t:rev", DETENT_OK) == 8);
		}
		{
			detent::testing::GenerationWriter writer(root / "later", detent::testing::GenerationWriter::Layout::PerNamespace);
			CHECK(get(empty.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // within 200 ms of the last try
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			CHECK(get(empty.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // change, no generation yet
			CHECK(writer.publish_json(knobs(7)) == 1);
			CHECK(get(empty.handle, "t:rev", DETENT_OK) == 7);
		}
	}
	{
		// A directory others can write is refused (D1 item 5: trust is checked, never inferred).
		const auto open_dir = root / "open";
		std::filesystem::create_directories(open_dir);
		::chmod(open_dir.c_str(), 0777);
		Opened refused(open_dir);
		CHECK(refused.status == DETENT_UNTRUSTED && refused.handle == nullptr);
		Opened foreign(root / "later", {}, ::getuid() + 1);
		CHECK(foreign.status == DETENT_UNTRUSTED);
	}
	{
		// D4 rule 5: `selected` without a valid `change` is refused at open.
		const auto store = root / "nochange";
		{
			detent::testing::GenerationWriter writer(store, detent::testing::GenerationWriter::Layout::PerNamespace);
			writer.publish_json(knobs(1));
		}
		::chmod((store / "change").c_str(), 0666);
		CHECK(Opened(store).status == DETENT_SOURCE_FAILURE);
		std::filesystem::remove(store / "change");
		CHECK(Opened(store).status == DETENT_SOURCE_FAILURE);
	}
}
} // namespace
int main() {
	const auto root = std::filesystem::current_path() / ".scratch" / ("reader-core-" + std::to_string(::getpid()));
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{root};
	atfork_refused(root); // first: the fork handlers register at the first open that succeeds
	fork_mid_open(root);
	replaced_selected(root);
	selected_reads(root);
	per_namespace(root);
	adding_namespace(root);
	entry_failure(root);
	unreadable_table(root);
	refresh_memo(root);
	replaced_change(root);
	const auto wide = wide_store(root);
	transient_miss(wide);
	capped(wide);
	retained(root);
	values(root);
	wrapper(root);
	opens(root);
	std::cout << "ok\n";
	return 0;
}
