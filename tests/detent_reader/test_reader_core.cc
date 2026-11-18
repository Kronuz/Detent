// The reader core's C ABI and C++ header (reader.md D1, D2, D4, D7; docs/mechanisms/reader-core.md).
#include "detent/reader.hh"
#include "detent_testing/generation_writer.hh"
#include "prepared_reader.hh"
#include "testing.hh"
#include <climits>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

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
	Opened(const std::filesystem::path &store, std::vector<detent_str> namespaces = {}, uid_t uid = ::getuid()) {
		const auto path = store.string();
		detent_open_options options{sizeof options, S(path), namespaces.data(), namespaces.size(), uint32_t(uid)};
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
	detent::testing::GenerationWriter writer(store);
	CHECK(writer.publish_json(knobs(1)) == 1);
	x::PreparedReaderTesting::install([](std::string_view name, void *context) {
		if (name != "selected")
			return;
		x::PreparedReaderTesting::install(nullptr, nullptr);
		auto &writer = *static_cast<detent::testing::GenerationWriter *>(context);
		CHECK(writer.publish_json(knobs(2)) == 2);
	}, &writer);
	struct Reset {
		~Reset() { x::PreparedReaderTesting::install(nullptr, nullptr); }
	} reset;
	Opened r(store);
	CHECK(r.status == DETENT_OK);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 2);
}
void replaced_change(const std::filesystem::path &root) {
	const auto store = root / "replaced-change";
	{
		detent::testing::GenerationWriter writer(store);
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
	detent::testing::GenerationWriter writer(store);
	CHECK(writer.publish_json(knobs(2)) == 2);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 2);
	std::uint64_t revision = 0;
	CHECK(detent_revision(r.handle, S("t"), &revision) == DETENT_OK && revision == 2);
}
void values(const std::filesystem::path &root) {
	const auto store = root / "values";
	detent::testing::GenerationWriter writer(store);
	CHECK(writer.publish_json(knobs(1)) == 1);
	Opened r(store);
	CHECK(r.status == DETENT_OK);
	// Lazy attach (D2): open attaches nothing; the first lookup attaches the one table entry.
	CHECK(t::stats(r.handle).attached == 0 && t::stats(r.handle).generations == 0);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 1);
	CHECK(t::stats(r.handle).attached == 1 && t::stats(r.handle).generations == 1);
	CHECK(get(r.handle, "t:min", DETENT_OK) == INT64_MIN && get(r.handle, "t:max", DETENT_OK) == INT64_MAX);
	CHECK(get(r.handle, "t:absent", DETENT_MISSING) == -7);
	CHECK(get(r.handle, "zz:absent", DETENT_MISSING) == -7);
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
	auto oracle = x::PreparedReader::open(store, ::getuid(), {});
	std::size_t compared = 0;
	for (const auto name : {"t:flag", "t:on", "t:size", "t:rev", "u:ramp", "u:all"})
		for (int h = -1; h != 200; ++h)
			for (const auto sw : {"", "vip", "half", "big", "other"}) {
				const std::string hash = "user" + std::to_string(h);
				const std::string_view qualified(name);
				const auto colon = qualified.find(':');
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
	CHECK(t::stats(r.handle).generations == 1 && t::stats(r.handle).live_views <= 4);
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
	const auto selected = x::PreparedReader::selection(store, ::getuid()).descriptor.generation;
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
		const auto generation = x::PreparedReader::selection(at, ::getuid()).descriptor.generation;
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
			    const auto generation = x::PreparedReader::selection(race.store, ::getuid()).descriptor.generation;
			    race.metadata = race.store / (detent::local::generation_name(generation) + ".read");
			    std::filesystem::rename(race.metadata, race.hidden);
		    } catch (...) {
			    std::abort();
		    }
	    },
	    &race);
	CHECK(get(r.handle, "t:rev", DETENT_OK) == 6 && race.fired == 2);
	t::install(nullptr, nullptr);
	std::filesystem::remove(root / "fifth.read");
	CHECK(t::stats(r.handle).generations == 1);
}
void wrapper(const std::filesystem::path &root) {
	const auto store = root / "wrapper";
	detent::testing::GenerationWriter writer(store);
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
			detent::testing::GenerationWriter staged(root / "staged");
			CHECK(moved.status == DETENT_OK && staged.publish_json(knobs(8)) == 1);
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			CHECK(get(moved.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // tries: no directory
			std::filesystem::rename(root / "staged", root / "moved");
			CHECK(get(moved.handle, "t:rev", DETENT_UNAVAILABLE) == -7); // within 200 ms of that try
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			CHECK(get(moved.handle, "t:rev", DETENT_OK) == 8);
		}
		{
			detent::testing::GenerationWriter writer(root / "later");
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
			detent::testing::GenerationWriter writer(store);
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
	replaced_selected(root);
	replaced_change(root);
	values(root);
	wrapper(root);
	opens(root);
	std::cout << "ok\n";
	return 0;
}
