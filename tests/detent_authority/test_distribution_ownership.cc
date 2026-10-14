#include "detent/scope.hh"
#include "service.hh"
#include <consensus/checkpoint.h>
#include <filesystem>
#include <http_owned_stream.h>
#include <iostream>
#include <completion/posix.h>
#include <rapidjson/document.h>
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
	bool hold_header = false;
	std::mutex header_mutex;
	std::optional<http::detail::OwnedStreamExchange::Write> header_original;
	std::string release_header() {
		std::optional<http::detail::OwnedStreamExchange::Write> original;
		{
			std::lock_guard lock(header_mutex);
			original = std::move(header_original);
			header_original.reset();
			hold_header = false;
		}
		check(bool(original));
		auto head = std::string(original->head);
		const auto serial = original->serial;
		original.reset();
		check(exchange->complete_write(serial, http::ReplySettlement::Written));
		return head;
	}
	~StreamWriter() override {
		if (exchange) {
			exchange->close(http::ReplySettlement::Shutdown);
			std::optional<std::uint64_t> serial;
			{
				std::lock_guard lock(header_mutex);
				hold_header = false;
				if (header_original) {
					serial = header_original->serial;
					header_original.reset();
				}
			}
			if (serial)
				exchange->complete_write(*serial, http::ReplySettlement::Shutdown);
			exchange->settle();
		}
	}
	http::OwnedStreamCapability stream(http::StreamOptions options) override {
		admission = options.reply.request_retention;
		auto resource = options.resource;
		exchange = std::allocate_shared<http::detail::OwnedStreamExchange>(
			http::detail::StreamAllocator<http::detail::OwnedStreamExchange>(resource), std::move(options),
			[this] {
				std::lock_guard lock(header_mutex);
				if (!hold_header)
					return;
				if (!header_original) {
					auto original = exchange->take_write();
					if (original)
						header_original = std::move(original);
				}
			});
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
void invoke(a::Service &service, Writer &writer, std::string path, std::string body,
			std::string token = "0123456789abcdef0123456789abcdef") {
	http::Request request;
	request.method = "POST";
	request.path = std::move(path);
	request.body = std::move(body);
	request.headers.emplace_back("Authorization", "Bearer " + token);
	service.handle(request, writer);
}
std::pair<std::string, std::string> receive(StreamWriter &writer, std::chrono::seconds timeout = 3s) {
	std::string head, body;
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!writer.exchange->settle()) {
		if (auto original = writer.exchange->take_write()) {
			head.append(original->head);
			body.append(original->chunk.bytes);
			const auto serial = original->serial;
			original.reset();
			check(writer.exchange->complete_write(serial, http::ReplySettlement::Written));
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			std::cerr << "stream timeout: header=" << head << ", body bytes=" << body.size() << '\n';
			check(false);
		}
		std::this_thread::sleep_for(1ms);
	}
	return {std::move(head), std::move(body)};
}
std::uint64_t seed_checkpoint(const a::ServiceOptions &options, std::size_t count) {
	a::State state(options.node.context, options.node.application);
	a::Command command;
	command.context = options.node.context;
	command.kind = a::Kind::Open;
	command.producer = "seed";
	command.nonce[0] = 's';
	std::uint64_t index = 1;
	check(state.apply(index, command).status == a::Status::Applied);
	command.kind = a::Kind::Transaction;
	command.nonce = {};
	command.generation = 1;
	command.name_space = "ns";
	for (std::size_t first = 0; first < count; first += 128) {
		++command.sequence;
		command.mutations.clear();
		for (std::size_t key = first; key < std::min(count, first + 128); ++key)
			command.mutations.push_back(
				{"ns:key" + std::to_string(key), detent::IntDef{static_cast<std::int64_t>(key)}, {}, {}});
		check(state.apply(++index, command).status == a::Status::Applied);
	}
	check(state.size() == count);
	const auto image = state.checkpoint();
	std::filesystem::create_directory(options.directory);
	std::filesystem::permissions(options.directory, std::filesystem::perms::owner_all);
	kronuz::journal::PosixIO io(options.directory);
	kronuz::journal::Store store(io, options.node.storage, 64 * 1024 * 1024);
	kronuz::journal::Identity identity{};
	identity[0] = 's';
	store.create(identity);
	while (!store.inventory_step(128).complete) {
	}
	const auto initialization = kronuz::consensus::encode_initialization(options.node.configuration);
	auto append = store.reserve_append(kronuz::journal::AdmissionClass::Control, initialization.size());
	check(bool(append));
	store.append(*append, initialization);
	auto replacement = store.reserve_replacement(
		image.size(), kronuz::consensus::checkpoint_detail::maximum_size(options.node.consensus));
	check(bool(replacement));
	store.begin_artifact(*replacement, kronuz::journal::ArtifactPart::Application);
	for (std::size_t offset = 0; offset < image.size(); offset += 65536)
		store.write_chunk(*replacement, std::string_view(image).substr(offset, 65536));
	const auto application = store.finish_artifact(*replacement);
	kronuz::consensus::RecoveredState recovered{
		options.node.configuration, {1, 1, index}, index, 1, {}, index};
	const auto bundle =
		kronuz::consensus::encode_checkpoint(recovered, 1, application, options.node.consensus, 0x44544101);
	store.begin_artifact(*replacement, kronuz::journal::ArtifactPart::Bundle);
	store.write_chunk(*replacement, bundle);
	store.finish_artifact(*replacement);
	store.publish(*replacement, 1);
	return state.revision();
}
} // namespace
int main(int argc, char **argv) {
	const bool publisher_pressure = argc == 2 && std::string_view(argv[1]) == "--poll-publisher";
	const std::size_t idle_keys = publisher_pressure										  ? 1
								  : argc == 2 && std::string_view(argv[1]) == "--idle-500000" ? 500000
								  : argc == 2 && std::string_view(argv[1]) == "--idle-100000" ? 100000
																							  : 0;
	const bool held_header = argc == 2 && std::string_view(argv[1]) == "--held-header";
	const bool large_scope =
		publisher_pressure || (argc == 2 && std::string_view(argv[1]) == "--large-scope");
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("delivery-ownership-" + std::to_string(getpid()));
		std::filesystem::create_directories(directory.parent_path());
		a::ServiceOptions options;
		options.directory = directory;
		options.requests = 8;
		options.timeout = 10s;
		if (held_header)
			options.delivery.bytes = 1;
		options.subscription_requests.polls = 12;
		options.subscription_requests.per_principal_polls = 12;
		options.subscription_requests.per_principal_controls = 16;
		options.node.context.cluster[0] = 'c';
		options.node.context.group[0] = 'g';
		options.node.context.epoch[0] = 'e';
		options.node.configuration.cluster = options.node.context.cluster;
		options.node.configuration.configuration = options.node.context.group;
		options.node.configuration.local = 1;
		options.node.configuration.voters = {1};
		options.credentials.push_back({"0123456789abcdef0123456789abcdef", "writer", {"ns"}});
		options.credentials.push_back(
			{"1123456789abcdef0123456789abcdef", "reader", {"ns"}, "writer", 1, false});
		options.credentials.push_back(
			{"2123456789abcdef0123456789abcdef", "reader", {"extra", "ns"}, "writer", 1, false});
		options.credentials.push_back(
			{"3123456789abcdef0123456789abcdef", "reader", {"secret"}, "writer", 1, false});
		options.credentials.push_back(
			{"4123456789abcdef0123456789abcdef", "reader", {"ns"}, "other", 1, false});
		options.credentials.push_back(
			{"5123456789abcdef0123456789abcdef", "reader", {"ns"}, "writer", 2, false});
		std::vector<std::string> names{"ns"};
		if (large_scope) {
			for (unsigned i = 0; i < 4094; ++i) {
				auto digits = std::to_string(i);
				names.push_back("ns" + std::string(4 - digits.size(), '0') + digits + std::string(190, 'x'));
			}
			for (auto index : {0u, 1u, 4u, 5u})
				options.credentials[index].namespaces = {names.begin(), names.end()};
			options.credentials[2].namespaces = {names.begin(), names.end()};
			options.credentials[2].namespaces.insert("extra");
			options.credentials[3].namespaces = {names.begin() + 1, names.end()};
			auto missing_last = options.credentials[1];
			missing_last.token = "6123456789abcdef0123456789abcdef";
			missing_last.namespaces.erase(names.back());
			options.credentials.push_back(std::move(missing_last));
			options.subscription_requests.open_bytes = 1024 * 1024;
		}
		if (idle_keys && !publisher_pressure)
			options.timeout = 120s;
		const auto initial_revision = idle_keys ? seed_checkpoint(options, idle_keys) : 0;
		const auto startup_begin = std::chrono::steady_clock::now();
		auto service = std::make_unique<a::Service>(options);
		if (idle_keys)
			std::cout << "authority startup ms="
					  << std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - startup_begin)
							 .count()
					  << std::endl;
		const auto identity = "\"cluster\":\"" + hex(options.node.context.cluster) + "\",\"group\":\"" +
							  hex(options.node.context.group) + "\",\"epoch\":\"" +
							  hex(options.node.context.epoch) + "\",";
		bool elected = false;
		for (unsigned turn = 0; turn < (idle_keys ? 6000u : 50u); ++turn) {
			Writer probe;
			invoke(*service, probe, "/v2/read", "{" + identity + "\"namespace\":\"ns\",\"keys\":[]}");
			if (probe.code == 200) {
				elected = true;
				break;
			}
			std::this_thread::sleep_for(20ms);
		}
		check(elected);
		std::vector<std::string_view> scope_names(names.begin(), names.end());
		const auto scope = d::canonical_scope_identity("writer", 1, scope_names);
		std::string encoded_names;
		for (const auto &name : names) {
			if (!encoded_names.empty())
				encoded_names += ',';
			encoded_names += '"' + name + '"';
		}
		const auto body = "{" + identity + "\"scope\":\"" + hex(scope) + "\",\"namespaces\":[" +
						  encoded_names + "],\"profile\":1,\"coding\":\"raw\"}";
		if (held_header) {
			StreamWriter failed;
			failed.hold_header = true;
			invoke(*service, failed, "/v2/distribution", body);
			const auto deadline = std::chrono::steady_clock::now() + 3s;
			for (;;) {
				{
					std::lock_guard lock(failed.header_mutex);
					if (failed.header_original)
						break;
				}
				check(std::chrono::steady_clock::now() < deadline);
				std::this_thread::sleep_for(1ms);
			}
			std::this_thread::sleep_for(50ms);
			check(!failed.admission.expired() && !failed.exchange->settle());
			const auto header = failed.release_header();
			auto response = receive(failed);
			check(header.starts_with("HTTP/1.1 503 ") && !response.second.empty());
			rapidjson::Document error;
			error.Parse(response.second.data(), response.second.size());
			check(!error.HasParseError() && error.HasMember("error"));
			service.reset();
			check(failed.admission.expired());
			std::filesystem::remove_all(directory);
			std::cout << "held legacy error header preserves control body and admission\n";
			return 0;
		}
		StreamWriter opened;
		invoke(*service, opened, "/v2/distribution/session/open", "{\"version\":1," + body.substr(1));
		auto negotiation = receive(opened);
		check(negotiation.first.starts_with("HTTP/1.1 200 "));
		rapidjson::Document session;
		session.Parse(negotiation.second.data(), negotiation.second.size());
		check(!session.HasParseError() && session["window"].GetUint64() == 0 &&
			  std::string_view(session["mode"].GetString()) == "snapshot");
		const auto handle =
			std::string("{\"version\":1,\"session\":\"") + session["session"].GetString() + "\"";
		StreamWriter snapshot;
		invoke(*service, snapshot, "/v2/distribution/session/next", handle + ",\"after\":0,\"wait_ms\":0}");
		auto update = receive(snapshot, idle_keys ? 120s : 3s);
		check(update.first.starts_with("HTTP/1.1 200 ") && !update.second.empty());
		check(update.first.find("X-Detent-Session-Version: 1\r\n") != std::string::npos);
		check(update.first.find("X-Detent-Window: 1\r\n") != std::string::npos);
		const auto published = "{" + identity + "\"scope\":\"" + hex(scope) +
							   "\",\"revision\":" + std::to_string(initial_revision) + "}";
		StreamWriter feedback;
		invoke(*service, feedback, "/v2/distribution/session/next",
			   handle + ",\"after\":1,\"published\":" + published + ",\"wait_ms\":0}");
		auto idle = receive(feedback);
		check(idle.first.starts_with("HTTP/1.1 204 ") && idle.second.empty());
		StreamWriter duplicate;
		invoke(*service, duplicate, "/v2/distribution/session/next",
			   handle + ",\"after\":1,\"published\":" + published + ",\"wait_ms\":0}");
		check(receive(duplicate).first.starts_with("HTTP/1.1 204 "));
		if (idle_keys) {
			auto health = [&] {
				std::this_thread::sleep_for(300ms);
				Writer writer;
				http::Request request;
				request.method = "GET";
				request.path = "/v1/health";
				service->handle(request, writer);
				check(writer.code == 200);
				rapidjson::Document result;
				result.Parse(writer.body.data(), writer.body.size());
				check(!result.HasParseError());
				return result;
			};
			const auto before = health();
			check(before["knobs"].GetUint64() == idle_keys && before["delivery_captures"].GetUint64() == 0);
			for (unsigned poll = 0; poll < 100; ++poll) {
				StreamWriter idle_poll;
				invoke(*service, idle_poll, "/v2/distribution/session/next",
					   handle + ",\"after\":1,\"published\":" + published + ",\"wait_ms\":0}");
				check(receive(idle_poll).first.starts_with("HTTP/1.1 204 "));
			}
			const auto after = health();
			check(after["subscription_quorum_reads"].GetUint64() ==
				  before["subscription_quorum_reads"].GetUint64());
			check(after["subscription_exports"].GetUint64() == before["subscription_exports"].GetUint64());
			check(after["delivery_captures"].GetUint64() == 0);
			if (publisher_pressure) {
				Writer producer;
				invoke(*service, producer, "/v2/open",
					   "{" + identity +
						   "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"}");
				check(producer.code == 200);
				std::atomic<unsigned> committed{0};
				const auto began = std::chrono::steady_clock::now();
				auto publishing = std::async(std::launch::async, [&] {
					for (unsigned sequence = 1; sequence <= 100; ++sequence) {
						Writer transaction;
						invoke(*service, transaction, "/v2/transactions",
							   "{" + identity + "\"generation\":1,\"sequence\":" + std::to_string(sequence) +
								   ",\"namespace\":\"ns\",\"operations\":[{\"key\":\"ns:key0\",\"value\":{"
								   "\"type\":\"int\",\"default\":0}}]}");
						if (transaction.code != 200 ||
							transaction.body.find("\"status\":\"no_change\"") == std::string::npos)
							std::cerr << "publisher sequence=" << sequence << " status=" << transaction.code
									  << " body=" << transaction.body << std::endl;
						check(transaction.code == 200 &&
							  transaction.body.find("\"status\":\"no_change\"") != std::string::npos);
						++committed;
					}
				});
				unsigned polls = 0, overlapping = 0;
				while (polls < 100 || publishing.wait_for(0ms) != std::future_status::ready) {
					if (committed.load() < 100)
						++overlapping;
					StreamWriter poll;
					invoke(*service, poll, "/v2/distribution/session/next",
						   handle + ",\"after\":1,\"published\":" + published + ",\"wait_ms\":0}");
					check(receive(poll).first.starts_with("HTTP/1.1 204 "));
					++polls;
					check(std::chrono::steady_clock::now() - began < 45s);
				}
				publishing.get();
				check(overlapping != 0 && committed.load() == 100);
				std::cout << "100 durable publisher transactions alongside " << polls
						  << " large-scope polls; overlapping polls=" << overlapping << std::endl;
			}
			Writer sampled;
			invoke(*service, sampled, "/v2/read",
				   "{" + identity + "\"namespace\":\"ns\",\"keys\":[\"ns:key" +
					   std::to_string(idle_keys - 1) + "\"]}");
			check(sampled.code == 200 &&
				  sampled.body.find("\"default\":" + std::to_string(idle_keys - 1)) != std::string::npos);
			StreamWriter closed;
			invoke(*service, closed, "/v2/distribution/session/close", handle + "}");
			check(receive(closed).first.starts_with("HTTP/1.1 204 "));
			service.reset();
			std::filesystem::remove_all(directory);
			std::cout << idle_keys
					  << " recovered authority knobs: 100 idle polls, zero additional quorum reads, exports "
						 "or captures\n";
			return 0;
		}
		std::vector<std::weak_ptr<void>> previous_polls;
		for (const auto &token : {"1123456789abcdef0123456789abcdef", "2123456789abcdef0123456789abcdef",
								  "2123456789abcdef0123456789abcdef"}) {
			StreamWriter rotated;
			invoke(*service, rotated, "/v2/distribution/session/next",
				   handle + ",\"after\":1,\"published\":" + published + ",\"wait_ms\":0}", token);
			check(receive(rotated).first.starts_with("HTTP/1.1 204 "));
			previous_polls.push_back(rotated.admission);
		}
		for (const auto &[token, status] : std::array<std::pair<std::string, std::string>, 3>{
				 {{"3123456789abcdef0123456789abcdef", "403"},
				  {"4123456789abcdef0123456789abcdef", "403"},
				  {"5123456789abcdef0123456789abcdef", "409"}}}) {
			StreamWriter forbidden;
			invoke(*service, forbidden, "/v2/distribution/session/next",
				   handle + ",\"after\":1,\"wait_ms\":0}", token);
			check(receive(forbidden).first.starts_with("HTTP/1.1 " + status + " "));
			previous_polls.push_back(forbidden.admission);
		}
		if (large_scope) {
			StreamWriter missing_last;
			invoke(*service, missing_last, "/v2/distribution/session/next",
				   handle + ",\"after\":1,\"wait_ms\":0}", "6123456789abcdef0123456789abcdef");
			check(receive(missing_last).first.starts_with("HTTP/1.1 403 "));
		}
		StreamWriter closed_session;
		invoke(*service, closed_session, "/v2/distribution/session/close", handle + "}");
		check(receive(closed_session).first.starts_with("HTTP/1.1 204 "));
		StreamWriter expired_session;
		invoke(*service, expired_session, "/v2/distribution/session/next", handle + ",\"after\":1}");
		check(receive(expired_session).first.starts_with("HTTP/1.1 410 "));
		const auto retired = std::chrono::steady_clock::now() + 3s;
		while (!opened.admission.expired() || !snapshot.admission.expired() ||
			   !feedback.admission.expired() || !duplicate.admission.expired() ||
			   std::any_of(previous_polls.begin(), previous_polls.end(),
						   [](const auto &poll) { return !poll.expired(); }) ||
			   !closed_session.admission.expired() || !expired_session.admission.expired()) {
			check(std::chrono::steady_clock::now() < retired);
			std::this_thread::sleep_for(1ms);
		}
		std::array<StreamWriter, 12> parked;
		std::vector<std::string> parked_handles;
		for (auto &writer : parked) {
			StreamWriter resume;
			invoke(*service, resume, "/v2/distribution/session/open",
				   "{\"version\":1," + body.substr(1, body.size() - 2) + ",\"base\":" + published + "}");
			auto response = receive(resume);
			check(response.first.starts_with("HTTP/1.1 200 "));
			rapidjson::Document negotiated;
			negotiated.Parse(response.second.data(), response.second.size());
			check(!negotiated.HasParseError() &&
				  std::string_view(negotiated["mode"].GetString()) == "resume");
			parked_handles.push_back(std::string("{\"version\":1,\"session\":\"") +
									 negotiated["session"].GetString() + "\"");
			invoke(*service, writer, "/v2/distribution/session/next",
				   parked_handles.back() + ",\"after\":0,\"wait_ms\":30000}");
			check(bool(writer.exchange));
		}
		Writer ordinary;
		invoke(*service, ordinary, "/v2/read", "{" + identity + "\"namespace\":\"ns\",\"keys\":[]}");
		check(ordinary.code == 200);
		Writer publisher;
		invoke(*service, publisher, "/v2/open",
			   "{" + identity + "\"expected_generation\":0,\"nonce\":\"6e000000000000000000000000000000\"}");
		check(publisher.code == 200);
		StreamWriter full_poll;
		invoke(*service, full_poll, "/v2/distribution/session/next", handle + ",\"after\":1}");
		check(full_poll.code == 503 && !full_poll.exchange);
		StreamWriter control;
		invoke(*service, control, "/v2/distribution/session/close", handle + "}");
		check(receive(control).first.starts_with("HTTP/1.1 204 "));
		for (auto &writer : parked)
			check(!writer.exchange->next_ready());
		if (large_scope) {
			std::array<StreamWriter, 12> closes;
			const auto began = std::chrono::steady_clock::now();
			for (std::size_t i = 0; i < closes.size(); ++i)
				invoke(*service, closes[i], "/v2/distribution/session/close", parked_handles[i] + "}",
					   "2123456789abcdef0123456789abcdef");
			Writer progressing;
			invoke(*service, progressing, "/v2/read", "{" + identity + "\"namespace\":\"ns\",\"keys\":[]}");
			check(progressing.code == 200);
			for (auto &writer : closes)
				check(receive(writer).first.starts_with("HTTP/1.1 204 "));
			std::cout << "4095-name rotated close batch ms="
					  << std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - began)
							 .count()
					  << '\n';
			for (auto &writer : parked)
				check(receive(writer).first.starts_with("HTTP/1.1 410 "));
		} else {
			for (auto &writer : parked) {
				writer.exchange->close(http::ReplySettlement::Shutdown);
				check(writer.exchange->settle());
			}
		}
		const auto legacy_scope =
			d::canonical_scope_identity("writer", 1, std::array<std::string_view, 1>{"ns"});
		const auto legacy_body = "{" + identity + "\"scope\":\"" + hex(legacy_scope) +
								 "\",\"namespaces\":[\"ns\"],\"profile\":1,\"coding\":\"raw\"}";
		StreamWriter first;
		invoke(*service, first, "/v2/distribution", legacy_body);
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
			invoke(*service, writer, "/v2/distribution", legacy_body);
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
