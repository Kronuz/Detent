#include "detent_distribution_http/session_request.hh"
#include <iostream>
#include <source_location>

namespace d = detent::distribution;
namespace h = detent::distribution_http;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("session request invariant at " + std::to_string(where.line()));
}
std::string identity(char value, std::size_t length = 32) { return "\"" + std::string(length, value) + "\""; }
std::string context() {
	return "\"cluster\":" + identity('1') + ",\"group\":" + identity('2') + ",\"epoch\":" + identity('3') +
		   ",\"scope\":" + identity('4', 64);
}
std::shared_ptr<d::OwnedBuffer> body(std::shared_ptr<d::StorageLedger> account, std::string_view bytes) {
	auto result = d::make_owned_buffer(account, bytes.size());
	result->append(bytes);
	result->seal();
	return result;
}
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
		{
			d::RetirementRuntime runtime(account);
			auto domain = runtime.domain(account, 64);
			std::string open = "{\"version\":1," + context() +
							   ",\"profile\":1,\"coding\":\"zstd\",\"namespaces\":[\"z\",\"app\",\"z\"],"
							   "\"force_snapshot\":true}";
			auto input = body(account, open);
			std::weak_ptr<d::OwnedBuffer> original = input;
			auto parsed = h::parse_session_request(domain, h::RequestKind::Open, input);
			input.reset();
			check(!original.expired());
			const auto &request = parsed->request();
			check(request.compressed && request.force_snapshot && request.namespaces.size() == 2);
			check(request.namespaces[0] == "app" && request.namespaces[1] == "z");
			check(request.identity.cluster[0] == '\x11' && request.identity.scope[0] == '\x44');
			parsed.reset();
			std::string next = "{\"version\":1,\"session\":" + identity('a') +
							   ",\"after\":7,\"wait_ms\":0,\"published\":{" + context() + ",\"revision\":9}}";
			parsed = h::parse_session_request(domain, h::RequestKind::Next, body(account, next));
			check(parsed->request().after == 7 && parsed->request().wait_ms == 0 &&
				  parsed->request().published->revision == 9);
			parsed.reset();
			parsed =
				h::parse_session_request(domain, h::RequestKind::Close,
										 body(account, "{\"version\":1,\"session\":" + identity('a') + "}"));
			check(parsed->request().kind == h::RequestKind::Close);
			parsed.reset();
			for (auto malformed :
				 {"{\"version\":1,\"session\":" + identity('a') + ",\"after\":0,\"after\":0}",
				  "{\"version\":1,\"session\":" + identity('a') + ",\"after\":0,\"extra\":0}",
				  "{\"version\":2,\"session\":" + identity('a') + ",\"after\":0}",
				  "{\"version\":1,\"session\":" + identity('A') + ",\"after\":0}",
				  "{\"version\":1,\"session\":" + identity('0') + ",\"after\":0}",
				  "{\"version\":1,\"session\":" + identity('a') + ",\"after\":-1}",
				  "{\"version\":1,\"session\":" + identity('a') + ",\"after\":0,\"wait_ms\":30001}",
				  "{\"version\":1,\"session\":" + identity('a') + ",\"after\":0,\"published\":null}",
				  "{\"version\":1,\"session\":" + identity('a') + ",\"after\":0,\"published\":{" + context() +
					  ",\"revision\":1,\"revision\":2}}",
				  std::string(65, '[') + "0" + std::string(65, ']'), std::string("{}\0", 3)}) {
				bool refused = false;
				try {
					h::parse_session_request(domain, h::RequestKind::Next, body(account, malformed));
				} catch (const std::invalid_argument &) {
					refused = true;
				}
				check(refused);
			}
			std::string names;
			for (unsigned i = 0; i < 4096; ++i) {
				if (i)
					names += ',';
				names += "\"ns" + std::to_string(i) + "\"";
			}
			open = "{\"version\":1," + context() + ",\"profile\":1,\"coding\":\"raw\",\"namespaces\":[" +
				   names + "]}";
			parsed = h::parse_session_request(domain, h::RequestKind::Open, body(account, open));
			check(parsed->request().namespaces.size() == 4096 && !parsed->request().compressed);
			std::cout << "{\"namespaces\":4096,\"parser_capacity\":" << parsed->storage_capacity() << "}\n";
			parsed.reset();
			bool refused = false;
			try {
				h::parse_session_request(domain, h::RequestKind::Open, body(account, open),
										 {16, 4096, 30000});
			} catch (const std::invalid_argument &) {
				refused = true;
			}
			check(refused);
			auto pressure_account = std::make_shared<d::StorageLedger>(1024 * 1024);
			{
				d::RetirementRuntime pressure_runtime(pressure_account);
				auto pressure_domain = pressure_runtime.domain(pressure_account, 4);
				auto input = body(pressure_account, "{\"version\":1,\"session\":" + identity('a') + "}");
				auto full = pressure_account->admit(pressure_account->limit() - pressure_account->used());
				refused = false;
				try {
					h::parse_session_request(pressure_domain, h::RequestKind::Close, input);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				check(refused && input->sealed());
				full.reset();
				input.reset();
				pressure_domain.close().wait();
			}
			check(pressure_account->used() == 0);
			{
				auto worker_result = h::parse_session_request_on_worker(account, h::RequestKind::Open, open);
				open.clear();
				check(worker_result->request().namespaces.size() == 4096);
				check(!worker_result->request().namespaces.front().empty());
			}

			check(h::classify_session_route("/v2/distribution/session/next") == h::RequestKind::Next);
			check(!h::classify_session_route("/v2/distribution/session/next/extra"));
			domain.close().wait();
			check(original.expired());
		}
		check(account->used() == 0);
		std::cout << "Strict request parsing, retained views, pressure and final drainage passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
