// Measure real distributor PUT/PATCH publication with nested override maps.
// Usage: detentd_publication_bench knobs PUT|PATCH iterations [directory]
#include "detentd.hh"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace {
thread_local bool measuring = false;
thread_local std::size_t allocations = 0, allocated_bytes = 0;
struct Writer : http::ResponseWriter {
	int code = 0;
	void status(int value) override { code = value; }
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view) override {}
	void end() override {}
	void set_close() override {}
};
std::string full_snapshot(std::size_t knobs, unsigned version) {
	std::string bytes = R"({"schema_version":1,"version":)" + std::to_string(version) + R"(,"knobs":{)";
	for (std::size_t i = 0; i < knobs; ++i) {
		if (i) bytes += ',';
		bytes += "\"bench:key" + std::to_string(i) + R"(":{"type":"bool","default":)" +
			((i + version) % 2 ? "true" : "false") +
			R"(,"overrides":{"canary":true,"production":{"rollout_pct":50},"internal":false}})";
	}
	bytes += "}}";
	return bytes;
}
void publish(detentd::Distributor& app, const http::Request& query) {
	Writer writer;
	app.handle(query, writer);
	if (writer.code != 200) throw std::runtime_error("benchmark publication refused");
}
} // namespace
void* operator new(std::size_t size) {
	if (measuring) { ++allocations; allocated_bytes += size; }
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
		if (argc != 4 && argc != 5) throw std::invalid_argument("knobs PUT|PATCH iterations [directory]");
		const auto knobs = std::stoull(argv[1]);
		const std::string mode = argv[2];
		const auto iterations = std::stoul(argv[3]);
		if (!knobs || knobs > 100000 || !iterations || iterations > 100 || (mode != "PUT" && mode != "PATCH"))
			throw std::invalid_argument("invalid publication workload");
		detentd::Options options;
		if (argc == 5) {
			const std::filesystem::path directory = argv[4];
			if (!std::filesystem::create_directory(directory)) throw std::runtime_error("benchmark directory exists");
			options.persist_path = directory / "snapshot.json";
		}
		detentd::Distributor app(options);
		http::Request query;
		query.method = "PUT"; query.path = "/v1/snapshot";
		query.body = full_snapshot(knobs, 1);
		publish(app, query);
		double seconds = 0;
		for (unsigned version = 2; version < iterations + 2; ++version) {
			query.method = mode;
			query.body = mode == "PUT" ? full_snapshot(knobs, version) :
				R"({"schema_version":1,"version":)" + std::to_string(version) +
				R"(,"knobs":{"bench:key0":{"type":"bool","default":)" + (version % 2 ? "true" : "false") +
				R"(,"overrides":{"canary":true,"production":{"rollout_pct":50},"internal":false}}}})";
			const auto started = std::chrono::steady_clock::now();
			measuring = true;
			publish(app, query);
			measuring = false;
			seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		}
		if (app.current_version() != iterations + 1 || app.puts_accepted() != iterations + 1)
			throw std::runtime_error("publication count mismatch");
		std::cout << "{\"knobs\":" << knobs << ",\"mode\":\"" << mode << "\",\"iterations\":" << iterations
			<< ",\"seconds\":" << seconds << ",\"allocations\":" << allocations
			<< ",\"allocated_bytes\":" << allocated_bytes << "}\n";
		return 0;
	} catch (const std::exception& error) {
		measuring = false;
		std::cerr << error.what() << '\n'; return 1;
	}
}
