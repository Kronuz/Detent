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
				auto worker_result = h::parse_session_request(account, h::RequestKind::Open, open);
				open.clear();
				check(worker_result->request().namespaces.size() == 4096);
				check(!worker_result->request().namespaces.front().empty());
			}

			// W5: [{name, cursor}] under the request's cluster, or `all` with a directory cursor.
			{
				auto v2 = [&](h::RequestKind kind, const std::string &json) {
					return h::parse_session_request(domain, kind, body(account, json));
				};
				auto refused_v2 = [&](h::RequestKind kind, const std::string &json) {
					try {
						v2(kind, json);
					} catch (const std::invalid_argument &) {
						return true;
					}
					return false;
				};
				auto opened = v2(h::RequestKind::Open,
								 "{\"version\":2,\"profile\":1,\"coding\":\"zstd\",\"cluster\":" + identity('1') +
									 ",\"namespaces\":[[\"app\"],[\"pay\"," + identity('2') + ",41]],\"all\":7,"
									 "\"wait_ms\":0}");
				const auto &request = opened->request();
				check(request.version == 2 && request.compressed && request.wait_ms == 0);
				check(request.positions.size() == 2 && request.positions[0].name == "app" &&
					  !request.positions[0].cursor && request.positions[1].name == "pay");
				d::Identity one, two;
				one.fill('\x11');
				two.fill('\x22');
				check(request.positions[1].cursor == d::NamespaceCursor{one, two, 41});
				check(request.all == d::DirectoryCursor{one, 7});
				opened.reset();
				// A first start: no cluster, no store, everything granted from the directory's start.
				opened = v2(h::RequestKind::Open,
							"{\"version\":2,\"profile\":1,\"coding\":\"raw\",\"namespaces\":[],\"all\":0}");
				check(opened->request().positions.empty() && opened->request().all == d::DirectoryCursor{} &&
					  opened->request().wait_ms == 30000);
				opened.reset();
				opened = v2(h::RequestKind::Next, "{\"version\":2,\"session\":" + identity('a') + ",\"cluster\":" +
													  identity('1') + ",\"namespaces\":[[\"pay\"," + identity('2') +
													  ",42]],\"wait_ms\":250}");
				check(opened->request().session[0] == '\xaa' && opened->request().positions.size() == 1 &&
					  opened->request().positions[0].cursor->position == 42 && !opened->request().all &&
					  opened->request().wait_ms == 250);
				opened.reset();
				opened = v2(h::RequestKind::Close, "{\"version\":2,\"session\":" + identity('a') + "}");
				check(opened->request().version == 2);
				opened.reset();
				const auto open2 = std::string("{\"version\":2,\"profile\":1,\"coding\":\"raw\",");
				for (auto bad : {
						 open2 + "\"namespaces\":[[\"b\"],[\"a\"]]}",					   // out of order
						 open2 + "\"namespaces\":[[\"a\"],[\"a\"]]}",					   // duplicate
						 open2 + "\"namespaces\":[[\"a\"," + identity('2') + ",1]]}",		   // cursor, no cluster
						 open2 + "\"namespaces\":[],\"all\":3}",								   // directory, no cluster
						 open2 + "\"namespaces\":[]}",										   // follows nothing
						 open2 + "\"namespaces\":[[\"a\"," + identity('2') + "]]}",			   // arity
						 open2 + "\"namespaces\":[[\"a b\"]]}",								   // name
						 open2 + "\"namespaces\":[\"a\"]}",									   // v1 entry
						 open2 + "\"cluster\":" + identity('1') + ",\"namespaces\":[[\"a\"," + identity('0') +
							 ",1]]}",															   // zero incarnation
						 open2 + "\"namespaces\":[[\"a\"]],\"wait_ms\":30001}",			   // wait bound
						 open2 + "\"namespaces\":[[\"a\"]],\"scope\":" + identity('4', 64) + "}", // v1 field
						 std::string("{\"version\":3,\"profile\":1,\"coding\":\"raw\",\"namespaces\":[[\"a\"]]}"),
					 })
					check(refused_v2(h::RequestKind::Open, bad));
				check(refused_v2(h::RequestKind::Next, "{\"version\":2,\"session\":" + identity('a') +
														   ",\"namespaces\":[[\"a\"]],\"after\":1}"));
				check(refused_v2(h::RequestKind::Close, "{\"version\":2,\"session\":" + identity('a') +
															",\"namespaces\":[]}"));
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
