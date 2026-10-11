#pragma once
#include "detent_relay/service.hh"
#include <rapidjson/document.h>
#include <sha256.h>

namespace relay_service_fixture {
namespace r = detent::relay;
class Writer final : public http::ResponseWriter {
	std::shared_ptr<d::StorageLedger> account_;

  public:
	std::shared_ptr<http::detail::OwnedStreamExchange> exchange;
	explicit Writer(std::shared_ptr<d::StorageLedger> account) : account_(std::move(account)) {}
	void status(int) override {}
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view) override {}
	void end() override {}
	void set_close() override {}
	http::OwnedStreamCapability stream(http::StreamOptions options) override {
		auto resource = options.resource;
		exchange = std::allocate_shared<http::detail::OwnedStreamExchange>(
			d::StorageAllocator<http::detail::OwnedStreamExchange>(account_), std::move(options), [] {});
		return http::detail::make_resource_unique<http::OwnedStreamReply>(std::move(resource), exchange);
	}
};
template <std::size_t N> std::string hex(const std::array<char, N> &value) {
	constexpr char digits[] = "0123456789abcdef";
	std::string result(N * 2, '0');
	for (std::size_t index = 0; index < N; ++index) {
		const auto byte = static_cast<unsigned char>(value[index]);
		result[2 * index] = digits[byte >> 4];
		result[2 * index + 1] = digits[byte & 15];
	}
	return result;
}
void qualify(std::shared_ptr<r::Bundle> bundle, d::RetirementHandle<d::EncodedUpdate> expected,
			 std::shared_ptr<d::StorageLedger> account, bool has_namespace) {
	const auto target = expected->target();
	const std::array<std::string_view, 1> names{"app"};
	const auto selected =
		has_namespace ? std::span<const std::string_view>(names) : std::span<const std::string_view>{};
	r::SessionAuthorization authorization;
	authorization.principal.identity.fill('p');
	authorization.principal.policy_epoch = 1;
	authorization.grants.fill('g');
	authorization.bundles[0] = 1;
	r::ConfiguredCredential credential;
	credential.authorization = authorization;
	const std::string token = "0123456789abcdef0123456789abcdef";
	class SHA256 hash;
	hash.add(token.data(), token.size());
	hash.getHash(credential.token_hash.data());
	auto metadata = std::make_shared<d::StorageLedger>(account, 2 * 1024 * 1024);
	auto registry = std::allocate_shared<r::Registry>(
		d::StorageAllocator<r::Registry>(metadata), metadata,
		std::array{r::RegistryBundle{target, authorization.principal, selected}}, std::array{credential});
	r::ServiceAccounts accounts{metadata,
								{std::make_shared<d::StorageLedger>(account, 4 * 1024 * 1024),
								 std::make_shared<d::StorageLedger>(account, 8 * 1024 * 1024),
								 std::make_shared<d::StorageLedger>(account, 2 * 1024 * 1024)}};
	const std::array bindings{r::SessionBinding{bundle, authorization.principal, target}};
	auto runtime = std::allocate_shared<r::ServiceRuntime>(d::StorageAllocator<r::ServiceRuntime>(metadata),
														   accounts, registry, bindings);
	auto service = std::make_unique<r::Service>(runtime);
	auto submit = [&](std::string path, std::string body, Writer &writer) {
		http::Request request;
		request.method = "POST";
		request.path = std::move(path);
		request.headers.emplace_back("Authorization", "Bearer " + token);
		auto sink = service->on_request_body(request);
		sink->write(body);
		sink->end();
		service->handle(request, writer);
	};
	auto drain = [&](Writer &writer) {
		std::string body;
		for (unsigned turn = 0; turn < 10000; ++turn) {
			runtime->step(r::Sessions::Clock::now());
			if (auto original = writer.exchange->take_write()) {
				body.append(original->chunk.bytes);
				check(writer.exchange->complete_write(original->serial, http::ReplySettlement::Written));
			}
			if (writer.exchange->settle())
				return body;
		}
		throw std::runtime_error("relay service fixture drainage timeout");
	};
	Writer open(account);
	submit("/v2/distribution/session/open",
		   "{\"version\":1,\"cluster\":\"" + hex(target.cluster) + "\",\"group\":\"" + hex(target.group) +
			   "\",\"epoch\":\"" + hex(target.restore_epoch) + "\",\"scope\":\"" + hex(target.scope) +
			   "\",\"namespaces\":" + (has_namespace ? "[\"app\"]" : "[]") +
			   ",\"coding\":\"raw\",\"profile\":1,\"force_snapshot\":true}",
		   open);
	const auto opening = drain(open);
	rapidjson::Document parsed;
	parsed.Parse(opening.data(), opening.size());
	check(!parsed.HasParseError() && parsed.HasMember("session") && parsed["session"].IsString());
	const std::string session = parsed["session"].GetString();
	Writer next(account);
	submit("/v2/distribution/session/next",
		   "{\"version\":1,\"session\":\"" + session + "\",\"after\":0,\"wait_ms\":0}", next);
	std::optional<http::detail::OwnedStreamExchange::Write> held;
	for (unsigned turn = 0; turn < 10000 && !held; ++turn) {
		runtime->step(r::Sessions::Clock::now());
		if (auto original = next.exchange->take_write()) {
			if (!original->chunk.bytes.empty())
				held = std::move(original);
			else
				check(next.exchange->complete_write(original->serial, http::ReplySettlement::Written));
		}
	}
	check(held && held->chunk.bytes == expected->frame(0, false)->view());
	check(runtime->status().bootstrap == 1);
	check(!next.exchange->complete_write(held->serial + 1, http::ReplySettlement::Written));
	Writer close(account);
	submit("/v2/distribution/session/close", "{\"version\":1,\"session\":\"" + session + "\"}", close);
	check(drain(close).empty());
	service.reset();
	for (unsigned turn = 0; turn < 100; ++turn)
		runtime->step(r::Sessions::Clock::now() + std::chrono::seconds(61));
	check(!runtime->drained() && runtime->status().requests[1] == 1);
	check(held->chunk.bytes == expected->frame(0, false)->view());
	check(!next.exchange->settle());
	check(next.exchange->complete_write(held->serial, http::ReplySettlement::Written));
	held.reset();
	check(next.exchange->settle());
	for (unsigned turn = 0; turn < 1000 && !runtime->drained(); ++turn)
		runtime->step(r::Sessions::Clock::now());
	check(runtime->drained() && runtime->status().bootstrap == 0 && runtime->status().peak_transfers == 1);
	std::cout << "relay HTTP Close, timeout and facade loss retain authentic binary originals\n";
}
} // namespace relay_service_fixture
