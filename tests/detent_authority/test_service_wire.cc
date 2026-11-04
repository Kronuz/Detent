// The authority's reply framing and reply budget, from the harness and from a real HttpAsioService with a
// raw socket: Connection follows request.keep_alive, an over-budget command answers outcome_unknown and a
// replay commits it, and the owned stream's write deadline leaves at least a second after the handler.
#include "service.hh"
#include "support/handler_harness.hh"
#include <asio.hpp>
#include <cassert>
#include <filesystem>
#include <http_asio.h>
#include <iostream>
#include <unistd.h>

using namespace detent::authority;
using namespace std::chrono_literals;
namespace {
struct DeadlineWriter : detent_test::Writer {
	std::chrono::steady_clock::time_point deadline{};
	http::OwnedStreamCapability stream(http::StreamOptions requested) override {
		deadline = requested.reply.deadline;
		return Writer::stream(std::move(requested));
	}
};
const std::string token = "0123456789abcdef0123456789abcdef";
const std::string identity =
	"\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
	"\"epoch\":\"65000000000000000000000000000000\",";
const std::string open_fields = "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"";
const std::string read_fields = "\"namespace\":\"ns\",\"keys\":[\"ns:key\",\"ns:key2\"]";
const auto timeout = 3000ms;

// Sequence 1 creates both keys; later commands update ns:key at revision 1.
const std::string initial =
	"\"generation\":1,\"sequence\":1,\"namespace\":\"ns\",\"operations\":["
	"{\"key\":\"ns:key\",\"value\":{\"type\":\"int\",\"default\":10},\"expected_revision\":0,\"absent_namespace_revision\":0},"
	"{\"key\":\"ns:key2\",\"value\":{\"type\":\"int\",\"default\":20},\"expected_revision\":0,\"absent_namespace_revision\":0}]";
const std::string update =
	"\"generation\":1,\"sequence\":2,\"namespace\":\"ns\",\"operations\":["
	"{\"key\":\"ns:key\",\"value\":{\"type\":\"int\",\"default\":12},\"expected_revision\":1}]";
ServiceOptions options(const std::filesystem::path &directory, std::size_t reply_bytes) {
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
	o.credentials.push_back({token, "writer", {"ns"}});
	o.timeout = timeout;
	o.reply_bytes = reply_bytes;
	return o;
}
DeadlineWriter call(Service &s, std::string path, std::string fields, bool keep_alive = true, std::string method = "POST") {
	http::Request r;
	r.method = std::move(method);
	r.path = std::move(path);
	r.keep_alive = keep_alive;
	r.headers.emplace_back("Authorization", "Bearer " + token);
	DeadlineWriter w;
	detent_test::run_handler(s, r, w, "{" + identity + fields + "}");
	return w;
}
void open_and_put(Service &s) {
	DeadlineWriter opened;
	for (int i = 0; i < 100 && opened.code != 200; ++i) {
		opened = call(s, "/v2/open", open_fields);
		if (opened.code != 200) std::this_thread::sleep_for(20ms);
	}
	assert(opened.code == 200);
	assert(call(s, "/v2/transactions", initial).code == 200);
}
std::string directory(const char *name) {
	auto d = std::filesystem::current_path() / ".scratch" / (std::string(name) + std::to_string(getpid()));
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d.parent_path());
	return d.string();
}
bool has(const std::string &text, std::string_view needle) { return text.find(needle) != std::string::npos; }

void keep_alive_headers() {
	const auto dir = directory("authority-wire-ka-");
	Service s(options(dir, 1 << 16));
	open_and_put(s);
	for (bool keep : {true, false}) {
		for (auto [path, fields] : {std::pair{std::string("/v2/transactions"), initial}, std::pair{std::string("/v2/read"), read_fields}}) {
			auto w = call(s, path, fields, keep);
			assert(w.code == 200);
			const auto head = w.wire_head.substr(0, w.wire_head.find("\r\n\r\n"));
			assert(has(head, "Connection: keep-alive") == keep);
			assert(has(head, "Connection: close") == !keep);
		}
	}
	std::filesystem::remove_all(dir);
}

void over_budget_command() {
	const auto dir = directory("authority-wire-budget-");
	Service s(options(dir, 1024));
	open_and_put(s);
	// A held read reply reserves its body plus 256 bytes, leaving too little for the command's reply.
	http::Request hr;
	hr.method = "POST";
	hr.path = "/v2/read";
	hr.headers.emplace_back("Authorization", "Bearer " + token);
	DeadlineWriter hold;
	detent_test::intake(s, hr, "{" + identity + read_fields + "}");
	detent_test::run_handle(s, hr, hold);
	auto held = hold.pull();
	assert(held && held->status == 200);
	const auto &command = update;
	{
		auto over = call(s, "/v2/transactions", command);
		assert(over.code == 503 && has(over.body, "outcome_unknown"));
	}
	hold.release(*held);
	hr.extension.reset();
	// The budget returns when the released write settles; the replay of the same sequence commits then.
	bool replayed = false;
	for (int attempt = 0; attempt < 200 && !replayed; ++attempt) {
		auto replay = call(s, "/v2/transactions", command);
		replayed = replay.code == 200;
		assert(replayed || has(replay.body, "outcome_unknown"));
		if (!replayed) std::this_thread::sleep_for(25ms);
	}
	assert(replayed);
	bool drained = false;
	for (int attempt = 0; attempt < 200 && !drained; ++attempt) {
		drained = has(call(s, "/v1/health", "", true, "GET").body, "\"reserved_reply_bytes\":0");
		if (!drained) std::this_thread::sleep_for(25ms);
	}
	assert(drained);
	std::filesystem::remove_all(dir);
}

void write_deadline_margin() {
	const auto dir = directory("authority-wire-deadline-");
	Service s(options(dir, 1 << 16));
	open_and_put(s);
	http::Request r;
	r.method = "POST";
	r.path = "/v2/read";
	r.headers.emplace_back("Authorization", "Bearer " + token);
	DeadlineWriter w;
	detent_test::intake(s, r, "{" + identity + read_fields + "}");
	detent_test::run_handle(s, r, w);
	const auto margin = w.deadline - std::chrono::steady_clock::now();
	assert(margin >= 1s);
	assert(margin <= timeout + 1s);
	w.wire();
	std::filesystem::remove_all(dir);
}

std::string request_text(const std::string &path, const std::string &fields, bool keep_alive) {
	const auto body = "{" + identity + fields + "}";
	return "POST " + path + " HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token +
		   "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
		   (keep_alive ? "" : "\r\nConnection: close") + "\r\n\r\n" + body;
}
std::size_t count_of(const std::string &text, std::string_view needle) {
	std::size_t n = 0;
	for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
	return n;
}
// Reads until `responses` replies have arrived whole, or the peer closes, or the limit passes.
std::string read_replies(asio::ip::tcp::socket &socket, std::size_t responses) {
	std::string out;
	const auto end = std::chrono::steady_clock::now() + 5s;
	socket.non_blocking(true);
	char buffer[4096];
	while (std::chrono::steady_clock::now() < end) {
		asio::error_code error;
		const auto n = socket.read_some(asio::buffer(buffer), error);
		out.append(buffer, n);
		if (error == asio::error::would_block) std::this_thread::sleep_for(5ms);
		else if (error) break;
		if (count_of(out, "HTTP/1.1 ") >= responses && out.back() == '}') break;
	}
	return out;
}

void keep_alive_on_the_wire() {
	const auto dir = directory("authority-wire-socket-");
	Service app(options(dir, 1024));
	http::HttpAsioService service(app, 1, 64);
	service.set_max_body(1 << 20);
	service.start(0);
	open_and_put(app);
	asio::io_context io;
	asio::ip::tcp::socket socket(io);
	socket.connect({asio::ip::make_address("127.0.0.1"), service.port()});
	const auto read = request_text("/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]", true);
	asio::write(socket, asio::buffer(read + read));
	auto out = read_replies(socket, 2);
	assert(count_of(out, "HTTP/1.1 200") == 2);
	assert(count_of(out, "Connection: keep-alive") == 2 && !has(out, "Connection: close"));
	asio::write(socket, asio::buffer(request_text("/v2/transactions", initial, true)));
	out = read_replies(socket, 1);
	assert(out.compare(0, 12, "HTTP/1.1 200") == 0 && has(out, "Connection: keep-alive"));
	asio::ip::tcp::socket closing(io);
	closing.connect({asio::ip::make_address("127.0.0.1"), service.port()});
	asio::write(closing, asio::buffer(request_text("/v2/read", "\"namespace\":\"ns\",\"keys\":[\"ns:key\"]", false)));
	out = read_replies(closing, 1);
	assert(out.compare(0, 12, "HTTP/1.1 200") == 0 && has(out, "Connection: close") && !has(out, "Connection: keep-alive"));
	socket.close();
	closing.close();
	service.stop();
	app.begin_shutdown();
	app.drain();
	app.owner().stop();
	service.close();
	app.owner().close();
	service.destroy_roots();
	app.owner().destroy_roots();
	std::filesystem::remove_all(dir);
}
} // namespace

int main() {
	keep_alive_headers();
	over_budget_command();
	write_deadline_margin();
	keep_alive_on_the_wire();
	std::cout << "Authority reply framing, reply budget and write deadline passed\n";
}
