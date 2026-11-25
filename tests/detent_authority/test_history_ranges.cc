// A history file several times the reply bound, read in ranges (PLAN.md D138): a voter refuses the
// whole file 413, answers each range of at most the bound from its offset (fewer bytes at the bound
// or the file's end), and the ranges join into the layout's exact bytes. A range at the end is 416;
// an offset without a length, or an empty range, is refused.
#include "detent_relay/layout.hh"
#include "service.hh"
#include "support/handler_harness.hh"
#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace detent::authority;
namespace r = detent::relay;
namespace {
void check(bool value, int line) {
	if (!value)
		throw std::runtime_error("check failed at line " + std::to_string(line));
}
#define CHECK(x) check(bool(x), __LINE__)

constexpr std::size_t reply_bytes = 4096, bound = reply_bytes - 256;
const std::string token = "0123456789abcdef0123456789abcdef";
const std::string authority =
	"\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
	"\"epoch\":\"65000000000000000000000000000000\",";

detent_test::Writer call(Service &service, std::string path, const std::string &body) {
	http::Request request;
	request.method = "POST";
	request.path = std::move(path);
	request.headers.emplace_back("Authorization", "Bearer " + token);
	detent_test::Writer writer;
	try {
		detent_test::run_handler(service, request, writer, body);
	} catch (const http::RequestRejected &error) {
		writer.code = error.status();
		writer.body = error.what();
	}
	return writer;
}
std::string file_body(const std::string &name, std::string extra = {}) {
	return "{\"namespace\":\"ns\",\"name\":\"" + name + "\"" + extra + "}";
}
std::string range(std::uint64_t offset, std::uint64_t length) {
	return ",\"offset\":" + std::to_string(offset) + ",\"length\":" + std::to_string(length);
}
ServiceOptions options(const std::filesystem::path &directory) {
	ServiceOptions o;
	o.directory = directory;
	o.node.context.cluster[0] = 'c';
	o.node.context.group[0] = 'g';
	o.node.context.epoch[0] = 'e';
	o.node.configuration.cluster = o.node.context.cluster;
	o.node.configuration.configuration = o.node.context.group;
	o.node.configuration.local = 1;
	o.node.configuration.voters = {1};
	o.node.timing = {.heartbeat = 20, .rpc_timeout = 40, .election_min = 100, .election_max = 200};
	// Small seals and roll-ups, and files that stay under a peer fetch: a short run writes a base.
	o.node.application.state_bytes = 8 << 20;
	auto &layout = o.node.application.layout;
	layout.seal_transactions = 3;
	layout.period = 4;
	layout.carry_bytes = 64;
	layout.carry_count = 2;
	layout.alpha_num = 8;
	layout.alpha_den = 1;
	o.credentials.push_back({token, "writer", {"ns"}});
	o.reply_bytes = reply_bytes;
	o.seal_after = std::chrono::milliseconds(50);
	o.timeout = std::chrono::milliseconds(3000);
	return o;
}
template <class Predicate> void until(Predicate ready, const char *what) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!ready()) {
		if (std::chrono::steady_clock::now() >= deadline)
			throw std::runtime_error(std::string("timed out: ") + what);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}
} // namespace

int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("authority-history-ranges-" + std::to_string(getpid()));
		std::filesystem::remove_all(directory);
		std::filesystem::remove_all(directory.string() + ".history");
		std::filesystem::create_directories(directory.parent_path());
		{
			Service service(options(directory));
			const std::string open = "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"";
			until([&] { return call(service, "/v2/open", "{" + authority + open + "}").code == 200; }, "open");
			// Write until the layout names a file several times the bound.
			std::uint64_t sequence = 0;
			r::MirrorFile large;
			until(
				[&] {
					std::string operations;
					for (unsigned i = 0; i < 100; ++i)
						operations += std::string(i ? "," : "") + "{\"key\":\"ns:k" + std::to_string(sequence * 100 + i) +
									  "\",\"value\":{\"default\":" + std::to_string(sequence * 7 + i) + "}}";
					const auto written = call(service, "/v2/transactions",
											  "{" + authority + "\"generation\":1,\"sequence\":" +
												  std::to_string(++sequence) +
												  ",\"namespace\":\"ns\",\"operations\":[" + operations + "]}");
					CHECK(written.code == 200);
					const auto layouts = call(service, "/v2/history/layouts", "{\"namespaces\":[\"ns\"]}");
					if (layouts.code != 200)
						return false;
					for (const auto &layout : r::decode_mirror_table(layouts.body).entries)
						for (const auto &file : layout.files)
							if (file.size > 4 * bound)
								large = file;
					return large.size > 0;
				},
				"a history file several times the reply bound");
			std::cout << "file " << large.name << ": " << large.size << " bytes, bound " << bound << "\n";

			// The whole file is above the bound.
			const auto whole = call(service, "/v2/history/file", file_body(large.name));
			CHECK(whole.code == 413 && whole.body.find("reply_bound") != std::string::npos);

			// Ranges of 1000 bytes, then ranges longer than the bound, which answer the bound.
			for (const std::uint64_t length : {std::uint64_t{1000}, std::uint64_t{1} << 20}) {
				std::string joined;
				unsigned requests = 0;
				while (joined.size() < large.size) {
					const auto answer = call(service, "/v2/history/file", file_body(large.name, range(joined.size(), length)));
					CHECK(answer.code == 200);
					CHECK(answer.body.size() == std::min<std::uint64_t>({length, bound, large.size - joined.size()}));
					joined += answer.body;
					++requests;
				}
				CHECK(joined.size() == large.size && r::digest_of(joined) == large.digest);
				CHECK(requests == (large.size + std::min<std::uint64_t>(length, bound) - 1) / std::min<std::uint64_t>(length, bound));
			}
			// A range out of order, from the middle: the file is the same bytes wherever a range starts.
			const auto middle = call(service, "/v2/history/file", file_body(large.name, range(large.size / 2, 10)));
			CHECK(middle.code == 200 && middle.body.size() == 10);

			const auto end = call(service, "/v2/history/file", file_body(large.name, range(large.size, 10)));
			CHECK(end.code == 416 && end.body.find("range_not_satisfiable") != std::string::npos);
			CHECK(call(service, "/v2/history/file", file_body(large.name, ",\"offset\":0")).code == 400);
			CHECK(call(service, "/v2/history/file", file_body(large.name, ",\"length\":10")).code == 400);
			CHECK(call(service, "/v2/history/file", file_body(large.name, range(0, 0))).code == 400);
			CHECK(call(service, "/v2/history/file", file_body("absent", range(0, 10))).code == 404);
		}
		std::filesystem::remove_all(directory);
		std::filesystem::remove_all(directory.string() + ".history");
		std::cout << "history ranges: ok\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "history ranges: " << error.what() << "\n";
		return 1;
	}
}
