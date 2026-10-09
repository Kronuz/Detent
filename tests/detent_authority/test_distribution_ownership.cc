#include "detent/scope.hh"
#include "service.hh"
#include <filesystem>
#include <http_owned_stream.h>
#include <iostream>
#include <source_location>
#include <unistd.h>
namespace a = detent::authority;
namespace d = detent::distribution;
using namespace std::chrono_literals;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("distribution ownership invariant at " + std::to_string(where.line()));
}
struct Writer : http::ResponseWriter {
	unsigned code = 0;
	std::string body;
	void status(int value) override { code = value; }
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view value) override { body.append(value); }
	void end() override {}
	void set_close() override {}
};
struct StreamWriter : Writer {
	std::shared_ptr<http::detail::OwnedStreamExchange> exchange;
	std::weak_ptr<void> admission;
	http::OwnedStreamCapability stream(http::StreamOptions options) override {
		admission = options.reply.request_retention;
		auto resource = options.resource;
		exchange = std::allocate_shared<http::detail::OwnedStreamExchange>(
			http::detail::StreamAllocator<http::detail::OwnedStreamExchange>(resource), std::move(options),
			[] {});
		return http::detail::make_resource_unique<http::OwnedStreamReply>(resource, exchange);
	}
};
std::string hex(std::span<const char> bytes) {
	std::string result;
	for (unsigned char byte : bytes) {
		result += "0123456789abcdef"[byte >> 4];
		result += "0123456789abcdef"[byte & 15];
	}
	return result;
}
void invoke(a::Service &service, Writer &writer, std::string path, std::string body) {
	http::Request request;
	request.method = "POST";
	request.path = std::move(path);
	request.body = std::move(body);
	request.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
	service.handle(request, writer);
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("delivery-ownership-" + std::to_string(getpid()));
		std::filesystem::create_directories(directory.parent_path());
		a::ServiceOptions options;
		options.directory = directory;
		options.requests = 8;
		options.timeout = 10s;
		options.node.context.cluster[0] = 'c';
		options.node.context.group[0] = 'g';
		options.node.context.epoch[0] = 'e';
		options.node.configuration.cluster = options.node.context.cluster;
		options.node.configuration.configuration = options.node.context.group;
		options.node.configuration.local = 1;
		options.node.configuration.voters = {1};
		options.credentials.push_back({"0123456789abcdef0123456789abcdef", "writer", {"ns"}});
		auto service = std::make_unique<a::Service>(options);
		const auto identity = "\"cluster\":\"" + hex(options.node.context.cluster) + "\",\"group\":\"" +
							  hex(options.node.context.group) + "\",\"epoch\":\"" +
							  hex(options.node.context.epoch) + "\",";
		bool elected = false;
		for (unsigned turn = 0; turn < 50; ++turn) {
			Writer probe;
			invoke(*service, probe, "/v2/read", "{" + identity + "\"namespace\":\"ns\",\"keys\":[]}");
			if (probe.code == 200) {
				elected = true;
				break;
			}
			std::this_thread::sleep_for(20ms);
		}
		check(elected);
		const auto scope = d::canonical_scope_identity("writer", 1, std::array<std::string_view, 1>{"ns"});
		const auto body = "{" + identity + "\"scope\":\"" + hex(scope) +
						  "\",\"namespaces\":[\"ns\"],\"profile\":1,\"coding\":\"raw\"}";
		StreamWriter first;
		invoke(*service, first, "/v2/distribution", body);
		check(bool(first.exchange));
		std::optional<http::detail::OwnedStreamExchange::Write> original;
		const auto deadline = std::chrono::steady_clock::now() + 3s;
		for (;;) {
			original = first.exchange->take_write();
			if (original && !original->chunk.bytes.empty())
				break;
			if (original) {
				const auto serial = original->serial;
				original.reset();
				check(first.exchange->complete_write(serial, http::ReplySettlement::Written));
			}
			check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::sleep_for(1ms);
		}
		std::weak_ptr<const void> frame = original->chunk.owner;
		check(!frame.expired() && !first.admission.expired());
		std::array<StreamWriter, 4> queued;
		for (auto &writer : queued) {
			invoke(*service, writer, "/v2/distribution", body);
			check(bool(writer.exchange));
		}
		// A held original occupies the distribution slot, while an ordinary
		// quorum read behind waiting delivery metadata still makes progress.
		Writer read;
		invoke(*service, read, "/v2/read", "{" + identity + "\"namespace\":\"ns\",\"keys\":[]}");
		check(read.code == 200);
		for (auto &writer : queued)
			check(!writer.exchange->next_ready());
		check(first.exchange->close(http::ReplySettlement::Shutdown));
		check(!first.exchange->settle() && !frame.expired() && !first.admission.expired());
		for (auto &writer : queued) {
			writer.exchange->close(http::ReplySettlement::Shutdown);
			check(writer.exchange->settle());
		}
		auto closed = std::async(std::launch::async, [&] { service.reset(); });
		check(closed.wait_for(50ms) == std::future_status::timeout);
		const auto serial = original->serial;
		original.reset();
		check(first.exchange->complete_write(serial, http::ReplySettlement::Shutdown));
		check(first.exchange->settle());
		check(closed.wait_for(3s) == std::future_status::ready);
		closed.get();
		check(frame.expired() && first.admission.expired());
		std::filesystem::remove_all(directory);
		std::cout << "distribution original and capture-slot ownership passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
