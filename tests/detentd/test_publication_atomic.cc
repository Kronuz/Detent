#include "detentd.hh"
#include "detent/detail/file.hh"
#include <http_conditional.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <new>
#include <source_location>
#include <sys/resource.h>
#include <unistd.h>
#include <utility>

namespace {
thread_local long fail_after = -1;
thread_local bool injected = false;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value) throw std::runtime_error("publication invariant at line " + std::to_string(where.line()));
}
struct Writer : http::ResponseWriter {
	int code = 0;
	std::string body;
	std::map<std::string, std::string, std::less<>> headers;
	std::function<void()> on_version;
	void status(int value) override { code = value; }
	void set_header(std::string_view key, std::string_view value) override {
		headers[std::string(key)] = value;
		if (key == "X-Snapshot-Version" && on_version) {
			auto callback = std::move(on_version);
			callback();
		}
	}
	void write(std::string_view bytes) override { body += bytes; }
	void end() override {}
	void set_close() override {}
};
std::string snapshot(unsigned version) {
	std::string groups = R"("a":true)";
	for (unsigned i = 0; i < 12; ++i) {
		groups += ",\"cohort-" + std::to_string(i) + R"(":{"rollout_pct":50})";
	}
	return R"({"schema_version":1,"version":)" + std::to_string(version) +
		R"(,"knobs":{"test:feature":{"type":"bool","default":)" + (version % 2 ? "true" : "false") +
		R"(,"overrides":{)" + groups + R"(}},"test:limit":{"type":"int","default":)" +
		std::to_string(version) + "}}}";
}
Writer request(detentd::Distributor& app, std::string method, std::string body = {}, std::string base = {}) {
	http::Request query;
	query.method = std::move(method);
	query.path = "/v1/snapshot";
	query.body = std::move(body);
	if (!base.empty()) query.headers.emplace_back("X-Detent-Base-Version", std::move(base));
	Writer writer;
	app.handle(query, writer);
	return writer;
}
void put(detentd::Distributor& app, unsigned version) {
	check(request(app, "PUT", snapshot(version)).code == 200);
}
std::string disk(const std::filesystem::path& path) {
	return detent::detail::read_file(path, 1024 * 1024);
}
struct Directory {
	std::filesystem::path path;
	explicit Directory(const char* parent) : path(std::filesystem::path(parent) / ("publication-" + std::to_string(::getpid()))) {
		check(std::filesystem::create_directory(path));
	}
	~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
struct FileLimit {
	struct rlimit prior{};
	using Handler = void (*)(int);
	Handler previous;
	FileLimit() : previous(std::signal(SIGXFSZ, SIG_IGN)) {
		check(::getrlimit(RLIMIT_FSIZE, &prior) == 0);
		auto limit = prior;
		limit.rlim_cur = 0;
		check(::setrlimit(RLIMIT_FSIZE, &limit) == 0);
	}
	~FileLimit() {
		if (::setrlimit(RLIMIT_FSIZE, &prior) != 0) std::abort();
		std::signal(SIGXFSZ, previous);
	}
};
void consistent_get() {
	detentd::Distributor app({});
	check(request(app, "GET").code == 404);
	put(app, 1);
	const auto before = request(app, "GET");
	http::Request query;
	query.method = "GET";
	query.path = "/v1/snapshot";
	Writer writer;
	writer.on_version = [&] { put(app, 2); };
	app.handle(query, writer);
	check(app.current_version() == 2 && writer.body == before.body);
	check(writer.headers.at("ETag") == http::weak_etag(writer.body));
	check(writer.headers.at("X-Snapshot-Version") == "1");
	const auto second = request(app, "GET");
	query.headers.emplace_back("If-None-Match", second.headers.at("ETag"));
	Writer conditional;
	conditional.on_version = [&] { put(app, 3); };
	app.handle(query, conditional);
	check(app.current_version() == 3 && conditional.code == 304 && conditional.body.empty());
	check(conditional.headers.at("ETag") == second.headers.at("ETag"));
	check(conditional.headers.at("X-Snapshot-Version") == "2");
}
void startup_identity(const std::filesystem::path& directory) {
	detentd::Options options;
	options.persist_path = directory / "startup-identity";
	const auto bytes = "  \n" + snapshot(9) + "\n  ";
	{
		std::ofstream out(options.persist_path);
		out.exceptions(std::ios::badbit | std::ios::failbit);
		out << bytes;
		out.close();
	}
	detentd::Distributor app(options);
	const auto loaded = request(app, "GET");
	check(app.current_version() == 9 && loaded.body == bytes && app.puts_accepted() == 0);
	check(loaded.headers.at("ETag") == http::weak_etag(bytes));
}
void persistence_failures(const std::filesystem::path& directory) {
	for (std::size_t history : {0u, 1u, 64u}) {
		detentd::Options options;
		options.persist_path = directory / ("state-" + std::to_string(history));
		options.max_diff_history = history;
		detentd::Distributor app(options);
		put(app, 1); put(app, 2);
		const auto before = request(app, "GET");
		const auto saved = disk(options.persist_path);
		std::filesystem::create_directory(options.persist_path.string() + ".tmp");
		bool failed = false;
		try { put(app, 3); } catch (const std::exception&) { failed = true; }
		check(failed && app.current_version() == 2 && app.puts_accepted() == 2);
		check(disk(options.persist_path) == saved && request(app, "GET").body == before.body);
		std::filesystem::remove(options.persist_path.string() + ".tmp");
		{
			FileLimit limit;
			failed = false;
			try { put(app, 3); } catch (const std::exception&) { failed = true; }
		}
		check(failed && disk(options.persist_path) == saved && request(app, "GET").body == before.body);
		put(app, 3); put(app, 4);
		check(app.puts_accepted() == 4);
		const auto old = request(app, "GET", {}, "1");
		check(old.headers.contains("X-Patch-Base-Version") == (history == 64));
		check(request(app, "GET", {}, "3").headers.at("X-Patch-Base-Version") == "3");
		detentd::Distributor restored(options);
		check(restored.current_version() == 4 && request(restored, "GET").body == disk(options.persist_path));
	}
	detentd::Options options;
	options.persist_path = directory / "rename-refused";
	detentd::Distributor app(options);
	std::filesystem::create_directory(options.persist_path);
	bool failed = false;
	try { put(app, 1); } catch (const std::exception&) { failed = true; }
	check(failed && app.current_version() == 0 && std::filesystem::is_directory(options.persist_path));
	std::filesystem::remove(options.persist_path);
	put(app, 1);
}
void history_rollback(const std::filesystem::path& directory) {
	detentd::Options options;
	options.persist_path = directory / "history-rollback";
	options.max_diff_history = 2;
	detentd::Distributor app(options);
	put(app, 1); put(app, 2); put(app, 3);
	const auto saved = disk(options.persist_path);
	const auto prime = request(app, "GET", {}, "1");
	check(prime.headers.at("X-Patch-Base-Version") == "1");
	for (bool limit_write : {false, true}) {
		bool failed = false;
		if (limit_write) {
			FileLimit limit;
			try { put(app, 4); } catch (const std::exception&) { failed = true; }
		} else {
			std::filesystem::create_directory(options.persist_path.string() + ".tmp");
			try { put(app, 4); } catch (const std::exception&) { failed = true; }
			std::filesystem::remove(options.persist_path.string() + ".tmp");
		}
		check(failed && app.current_version() == 3 && disk(options.persist_path) == saved);
		const auto before = app.diffs_composed();
		const auto recovered = request(app, "GET", {}, "1");
		check(recovered.body == prime.body && recovered.headers.at("X-Snapshot-Version") == "3");
		check(recovered.headers.at("X-Patch-Base-Version") == "1" && app.diffs_composed() == before + 1);
		check(request(app, "GET", {}, "1").body == prime.body && app.diffs_composed() == before + 1);
	}
	put(app, 4);
	check(!request(app, "GET", {}, "1").headers.contains("X-Patch-Base-Version"));
	check(request(app, "GET", {}, "2").headers.at("X-Patch-Base-Version") == "2");
}
void in_flight_composition(const std::filesystem::path& directory) {
	for (bool refuse : {false, true}) {
		detentd::Options options;
		options.persist_path = directory / (refuse ? "composed-refusal" : "composed-acceptance");
		detentd::Distributor app(options);
		put(app, 1); put(app, 2); put(app, 3);
		if (refuse) std::filesystem::create_directory(options.persist_path.string() + ".tmp");
		http::Request query;
		query.method = "GET"; query.path = "/v1/snapshot";
		query.headers.emplace_back("X-Detent-Base-Version", "1");
		Writer writer;
		writer.on_version = [&] {
			bool failed = false;
			try { put(app, 4); } catch (const std::exception&) { failed = true; }
			check(failed == refuse);
		};
		app.handle(query, writer);
		check(writer.headers.at("X-Snapshot-Version") == "3" && writer.headers.at("X-Patch-Base-Version") == "1");
		auto old = detent::parse_snapshot_patch(writer.body, "test");
		check(old && old.value().version == 3 && app.diffs_composed() == 1);
		const auto fresh = request(app, "GET", {}, "1");
		auto patch = detent::parse_snapshot_patch(fresh.body, "test");
		check(patch && patch.value().version == (refuse ? 3u : 4u));
		check(app.diffs_composed() == 2 && request(app, "GET", {}, "1").body == fresh.body);
		check(app.diffs_composed() == 2);
		if (refuse) {
			std::filesystem::remove(options.persist_path.string() + ".tmp");
			put(app, 4);
		}
	}
}
void allocation_failures(const std::filesystem::path& directory, const std::string& method) {
	unsigned failures = 0;
	for (long position = 0; position < 2000; ++position) {
		detentd::Options options;
		options.persist_path = directory / "allocation-state";
		std::filesystem::remove(options.persist_path);
		detentd::Distributor app(options);
		put(app, 1);
		const auto saved = disk(options.persist_path);
		const auto before = request(app, "GET");
		const auto input = snapshot(2);
		http::Request query;
		query.method = method; query.path = "/v1/snapshot"; query.body = input;
		Writer writer;
		injected = false;
		fail_after = position;
		try { app.handle(query, writer); }
		catch (...) { fail_after = -1; }
		fail_after = -1;
		if (!injected) {
			check(app.current_version() == 2 && failures > 20);
			std::cout << method << " allocation failure positions: " << failures << '\n';
			return;
		}
		++failures;
		if (app.current_version() == 1) {
			check(app.puts_accepted() == 1 && disk(options.persist_path) == saved);
			check(request(app, "GET").body == before.body);
			put(app, 2);
		} else {
			check(app.current_version() == 2 && app.puts_accepted() == 2);
			check(request(app, "GET").body == disk(options.persist_path));
		}
		check(app.current_version() == 2 && app.puts_accepted() == 2);
	}
	throw std::runtime_error("allocation sweep did not finish");
}
} // namespace
void* operator new(std::size_t size) {
	if (fail_after >= 0 && fail_after-- == 0) {
		fail_after = -1;
		injected = true;
		throw std::bad_alloc();
	}
	if (auto pointer = std::malloc(size ? size : 1)) return pointer;
	throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
int main(int argc, char** argv) {
	try {
		check(argc == 2);
		Directory directory(argv[1]);
		consistent_get();
		startup_identity(directory.path);
		persistence_failures(directory.path);
		history_rollback(directory.path);
		in_flight_composition(directory.path);
		allocation_failures(directory.path, "PUT");
		allocation_failures(directory.path, "PATCH");
		std::cout << "publication, persistence and validator consistency pass\n";
		return 0;
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
