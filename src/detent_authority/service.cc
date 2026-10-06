#include "service.hh"
#include "detent/json_schema.hh"
#include "detent/json_schema_internal.hh"
#include "detent/validation.hh"
#include <asio.hpp>
#include <fcntl.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <journal/asio_completion.h>
#include <journal/bsd_completion.h>
#endif
#include <fstream>
#include <journal/posix.h>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sha256.h>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::authority {
namespace {
using Clock = std::chrono::steady_clock;
void check(bool b, const char *message) {
	if (!b) {
		throw std::invalid_argument(message);
	}
}
using Object = rapidjson::Value;
void fields(const Object &v, std::initializer_list<std::string_view> allowed) {
	check(v.IsObject(), "expected object");
	std::set<std::string_view> seen;
	for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
		std::string_view key(it->name.GetString(), it->name.GetStringLength());
		check(seen.insert(key).second && std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
			  "unknown or duplicate field");
	}
}
const Object &field(const Object &v, const char *key) {
	check(v.HasMember(key), "missing field");
	return v[key];
}
std::string text(const Object &v, std::size_t cap = 200) {
	check(v.IsString() && v.GetStringLength() <= cap, "invalid string");
	auto value = std::string(v.GetString(), v.GetStringLength());
	check(value.find('\0') == std::string::npos, "NUL in string");
	return value;
}
std::uint64_t number(const Object &v) {
	check(v.IsUint64(), "invalid unsigned integer");
	return v.GetUint64();
}
std::optional<std::uint64_t> optional(const Object &v, const char *key) {
	return v.HasMember(key) ? std::optional<std::uint64_t>{number(v[key])} : std::nullopt;
}
Identity identity(std::string_view hex) {
	check(hex.size() == 32, "identity requires 32 hex characters");
	Identity id{};
	auto nibble = [](char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		throw std::invalid_argument("invalid identity hex");
	};
	for (unsigned i = 0; i < 16; ++i) {
		id[i] = static_cast<char>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
	}
	check(id != Identity{}, "zero identity");
	return id;
}
std::string hex(Identity id) {
	constexpr char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char byte : id) {
		out += digits[byte >> 4];
		out += digits[byte & 15];
	}
	return out;
}
void unique_members(const rapidjson::Value &value) {
	if (value.IsObject()) {
		std::set<std::string_view> names;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			check(names.insert({it->name.GetString(), it->name.GetStringLength()}).second,
				  "duplicate JSON field");
			unique_members(it->value);
		}
	} else if (value.IsArray())
		for (const auto &item : value.GetArray())
			unique_members(item);
}
rapidjson::Document document(std::string_view bytes, std::size_t cap) {
	check(bytes.size() <= cap && bytes.find('\0') == std::string_view::npos &&
			  !detent::internal::nesting_depth_exceeds(bytes, 64),
		  "JSON bound");
	rapidjson::Document d;
	d.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																					bytes.size());
	check(!d.HasParseError() && d.IsObject(), "invalid JSON object");
	unique_members(d);
	return d;
}
std::string result_json(Result r) {
	constexpr const char *names[] = {
		"invalid",			 "applied",		   "no_change",	   "conflict",
		"unknown_session",	 "session_fenced", "out_of_order", "result_unavailable",
		"identity_conflict", "capacity",	   "exhausted"};
	return "{\"status\":\"" + std::string(names[static_cast<unsigned>(r.status)]) +
		   "\",\"revision\":" + std::to_string(r.revision) +
		   ",\"namespace_revision\":" + std::to_string(r.namespace_revision) +
		   ",\"generation\":" + std::to_string(r.generation) + ",\"sequence\":" + std::to_string(r.sequence) +
		   "}";
}
int result_status(Result r) {
	switch (r.status) {
	case Status::Applied:
	case Status::NoChange:
		return 200;
	case Status::Capacity:
		return 429;
	default:
		return 409;
	}
}
// One owner-local JSON buffer. Check the actual encoded bound before every byte;
// only one definition DOM is live at a time, and no historical State is retained.
class BoundedReplyBuffer {
  public:
	using Ch = char;
	explicit BoundedReplyBuffer(std::size_t limit) : limit_(limit) {}
	void Put(char value) {
		if (bytes.size() == limit_) {
			throw std::length_error("authority reply bound");
		}
		bytes += value;
	}
	void Flush() {}
	std::string bytes;

  private:
	std::size_t limit_;
};
std::string read_reply(const State &state, std::string_view name_space, const std::vector<std::string> &keys,
					   std::size_t limit) {
	BoundedReplyBuffer buffer(limit);
	rapidjson::Writer<BoundedReplyBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("namespace_revision");
	writer.Uint64(state.namespace_revision(name_space));
	writer.Key("key_revisions");
	writer.StartObject();
	for (const auto &key : keys) {
		if (auto record = state.find(key)) {
			writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
			writer.Uint64(record->revision);
		}
	}
	writer.EndObject();
	writer.Key("schema_version");
	writer.Uint(1);
	writer.Key("version");
	writer.Uint64(state.revision());
	writer.Key("knobs");
	writer.StartObject();
	for (const auto &key : keys) {
		if (auto record = state.find(key)) {
			writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
			rapidjson::Document scratch;
			auto value = detent::internal::write_knob_def(*record->value, scratch.GetAllocator());
			value.Accept(writer);
		}
	}
	writer.EndObject();
	writer.EndObject();
	return std::move(buffer.bytes);
}
class Sink final : public http::BodySink {
	std::string &body_;
	std::size_t cap_;

  public:
	Sink(std::string &body, std::size_t cap) : body_(body), cap_(cap) {}
	void write(std::string_view bytes) override {
		check(body_.size() <= cap_ && bytes.size() <= cap_ - body_.size(), "authority body bound");
		body_.append(bytes);
	}
	void end() override {}
};
} // namespace
ServiceOptions load_service_options(const std::filesystem::path &path) {
	int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		throw std::runtime_error("cannot open authority configuration");
	}
	struct Close {
		int fd;
		~Close() { ::close(fd); }
	} close{fd};
	struct stat status{};
	check(::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
			  (status.st_uid == geteuid() || status.st_uid == 0) && !(status.st_mode & 0007) &&
			  !(status.st_mode & 0022) && status.st_size >= 0 && status.st_size <= 1024 * 1024,
		  "authority configuration ownership or size");
	std::string bytes(static_cast<std::size_t>(status.st_size), '\0');
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		auto n = ::read(fd, bytes.data() + offset, bytes.size() - offset);
		if (n <= 0) {
			throw std::runtime_error("short authority configuration");
		}
		offset += static_cast<std::size_t>(n);
	}
	auto d = document(bytes, 1024 * 1024);
	fields(d, {"cluster", "group", "epoch", "directory", "credentials", "node", "voters", "peer_tls",
			   "publisher_tls", "publisher_bind", "publisher_name"});
	ServiceOptions o;
	o.node.context = {identity(text(field(d, "cluster"))), identity(text(field(d, "group"))),
					  identity(text(field(d, "epoch")))};
	o.node.configuration.cluster = o.node.context.cluster;
	o.node.configuration.configuration = o.node.context.group;
	o.node.configuration.local = 1;
	o.node.configuration.voters = {1};
	o.directory = text(field(d, "directory"), 4096);
	if (o.directory.is_relative()) {
		o.directory = path.parent_path() / o.directory;
	}
	auto tls = [&](const Object &value) {
		fields(value, {"certificate", "key", "ca"});
		auto resolve = [&](const char *key) {
			std::filesystem::path p(text(field(value, key), 4096));
			return (p.is_relative() ? path.parent_path() / p : p).string();
		};
		return transport::TlsOptions{resolve("certificate"), resolve("key"), resolve("ca")};
	};
	check(d.HasMember("node") == d.HasMember("voters"), "node and voters must be configured together");
	if (d.HasMember("voters")) {
		o.node.configuration.local = number(field(d, "node"));
		o.node.configuration.voters.clear();
		transport::PeerOptions peers;
		peers.tls = tls(field(d, "peer_tls"));
		auto &voters = field(d, "voters");
		check(voters.IsArray() && voters.Size() >= 3 && voters.Size() <= o.node.consensus.voters &&
				  voters.Size() % 2,
			  "fixed voter count");
		std::set<asio::ip::tcp::endpoint> addresses;
		for (const auto &voter : voters.GetArray()) {
			fields(voter, {"id", "host", "port", "certificate_sha256"});
			auto id = number(field(voter, "id")), port = number(field(voter, "port"));
			check(id && port && port <= 65535, "voter ID or port");
			auto address = asio::ip::make_address(text(field(voter, "host")));
			asio::ip::tcp::endpoint endpoint(address, static_cast<unsigned short>(port));
			check(!address.is_unspecified() && !address.is_multicast() && addresses.insert(endpoint).second,
				  "voter address");
			auto hex = text(field(voter, "certificate_sha256"));
			check(hex.size() == 64, "certificate fingerprint length");
			transport::Fingerprint fingerprint;
			auto nibble = [](char c) -> unsigned {
				if (c >= '0' && c <= '9')
					return c - '0';
				if (c >= 'a' && c <= 'f')
					return c - 'a' + 10;
				if (c >= 'A' && c <= 'F')
					return c - 'A' + 10;
				throw std::invalid_argument("certificate fingerprint hex");
			};
			for (unsigned i = 0; i != fingerprint.size(); ++i)
				fingerprint[i] =
					static_cast<unsigned char>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
			check(peers.pins.emplace(id, fingerprint).second, "duplicate voter");
			o.node.configuration.voters.push_back(id);
			if (id == o.node.configuration.local)
				peers.listen = endpoint;
			else
				peers.addresses.emplace(id, endpoint);
		}
		check(peers.listen.port(), "local voter missing");
		o.peers = std::move(peers);
		// Production elections must cover durable vote/term barriers as well as
		// network latency. The Core defaults are intended for small fixtures.
		o.node.timing.heartbeat = 100;
		o.node.timing.rpc_timeout = 500;
		o.node.timing.election_min = 1000;
		o.node.timing.election_max = 2000;
		o.publisher_tls = tls(field(d, "publisher_tls"));
	} else {
		check(!d.HasMember("peer_tls"), "peer TLS requires fixed voters");
		if (d.HasMember("publisher_tls"))
			o.publisher_tls = tls(field(d, "publisher_tls"));
	}
	if (d.HasMember("publisher_bind"))
		o.publisher_bind = asio::ip::make_address(text(field(d, "publisher_bind"), 256));
	check(!o.publisher_bind.is_multicast() && (o.publisher_bind.is_loopback() || o.publisher_tls),
		  "non-loopback publishers require TLS");
	if (d.HasMember("publisher_name"))
		o.publisher_name = text(field(d, "publisher_name"), 253);
	check(o.publisher_name.find_first_of(" \t\r\n") == std::string::npos, "publisher TLS name");
	auto &credentials = field(d, "credentials");
	check(credentials.IsArray() && !credentials.Empty() && credentials.Size() <= o.node.application.producers,
		  "credential count bound");
	std::set<std::string> tokens, producers;
	for (const auto &value : credentials.GetArray()) {
		fields(value, {"token", "producer", "namespaces"});
		Credential c{text(field(value, "token"), 128), text(field(value, "producer"), 128), {}};
		check(c.token.size() >= 32 && !c.producer.empty() && tokens.insert(c.token).second &&
				  producers.insert(c.producer).second,
			  "invalid or duplicate credentials");
		auto &spaces = field(value, "namespaces");
		check(spaces.IsArray() && !spaces.Empty() && spaces.Size() <= o.node.application.namespaces,
			  "namespace authorization bound");
		for (const auto &name : spaces.GetArray()) {
			auto s = text(name);
			check(detent::is_valid_knob_name(s + ":x") && c.namespaces.insert(s).second,
				  "invalid namespace authorization");
		}
		o.credentials.push_back(std::move(c));
	}
	return o;
}
struct Service::Accounting {
	std::mutex mutex;
	std::vector<Credential> credentials;
	std::map<std::string, std::size_t> producers;
	std::size_t requests = 0, body_bytes = 0, reply_bytes = 0;
};
struct Service::Admission {
	std::shared_ptr<Accounting> accounting;
	const Credential *credential;
	std::size_t bytes;
	bool active = false;
	~Admission() {
		if (!active) {
			return;
		}
		std::lock_guard lock(accounting->mutex);
		--accounting->requests;
		accounting->body_bytes -= bytes;
		auto found = accounting->producers.find(credential->producer);
		if (found != accounting->producers.end() && --found->second == 0) {
			accounting->producers.erase(found);
		}
	}
};
struct Service::ReplyAdmission {
	std::shared_ptr<Accounting> accounting;
	std::size_t bytes = 0;
	bool active = false;
	~ReplyAdmission() {
		if (active) {
			std::lock_guard lock(accounting->mutex);
			accounting->reply_bytes -= bytes;
		}
	}
};
Service::Service(ServiceOptions options) : options_(std::move(options)) {
	check(options_.requests && options_.requests <= 65536 &&
			  options_.queued_bytes >= options_.node.application.request_bytes &&
			  options_.timeout.count() > 0 && options_.reply_bytes >= 1024 && !options_.credentials.empty(),
		  "invalid authority service limits");
	accounting_ = std::make_shared<Accounting>();
	accounting_->credentials = std::move(options_.credentials);
	std::promise<void> ready;
	auto future = ready.get_future();
	thread_ = std::thread([this, ready = std::move(ready)]() mutable { run(std::move(ready)); });
	try {
		future.get();
	} catch (...) {
		thread_.join();
		throw;
	}
}
Service::~Service() {
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	if (thread_.joinable()) {
		thread_.join();
	}
}
std::shared_ptr<Service::Admission> Service::admit(const http::Request &request) {
	auto header = request.header("Authorization");
	check(header.starts_with("Bearer ") && header.size() <= 135, "unauthorized authority request");
	auto token = header.substr(7);
	class SHA256 supplied;
	supplied.add(token.data(), token.size());
	Digest want{};
	supplied.getHash(want.data());
	const Credential *found = nullptr;
	for (const auto &c : accounting_->credentials) {
		class SHA256 stored;
		stored.add(c.token.data(), c.token.size());
		Digest have{};
		stored.getHash(have.data());
		unsigned difference = 0;
		for (unsigned i = 0; i < 32; ++i) {
			difference |= want[i] ^ have[i];
		}
		if (!difference) {
			found = &c;
		}
	}
	check(found != nullptr, "unauthorized authority request");
	auto bytes = options_.node.application.request_bytes;
	auto admission = std::make_shared<Admission>(Admission{accounting_, found, bytes});
	std::lock_guard lock(mutex_);
	std::lock_guard account_lock(accounting_->mutex);
	auto per_producer = accounting_->producers.find(found->producer);
	check(!stopping_ && accounting_->requests < options_.requests &&
			  (per_producer == accounting_->producers.end() || per_producer->second < 16) &&
			  bytes <= options_.queued_bytes - accounting_->body_bytes,
		  "authority admission full");
	++accounting_->producers[found->producer];
	++accounting_->requests;
	accounting_->body_bytes += bytes;
	admission->active = true;
	return admission;
}
std::unique_ptr<http::BodySink> Service::on_request_body(http::Request &request) {
	if (request.path == "/v1/health" && request.method == "GET") {
		return std::make_unique<Sink>(request.body, 0);
	}
	request.user_data = admit(request);
	return std::make_unique<Sink>(request.body, options_.node.application.request_bytes);
}
std::optional<Service::Reply> Service::execute(const http::Request &request,
											   std::shared_ptr<Admission> admission,
											   std::function<void(Reply)> complete) {
	check(request.method == "POST", "authority endpoints require POST");
	auto d = document(request.body, options_.node.application.request_bytes);
	auto request_fields = [&](const Object &value, std::initializer_list<std::string_view> allowed) {
		check(identity(text(field(value, "cluster"))) == options_.node.context.cluster &&
				  identity(text(field(value, "group"))) == options_.node.context.group &&
				  identity(text(field(value, "epoch"))) == options_.node.context.epoch,
			  "foreign request authority");
		std::set<std::string_view> seen;
		for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
			std::string_view key(it->name.GetString(), it->name.GetStringLength());
			check(seen.insert(key).second &&
					  (key == "cluster" || key == "group" || key == "epoch" ||
					   std::find(allowed.begin(), allowed.end(), key) != allowed.end()),
				  "unknown or duplicate request field");
		}
	};
	auto task = std::make_shared<Task>();
	task->admission = std::move(admission);
	task->complete = std::move(complete);
	task->command.context = options_.node.context;
	task->command.producer = task->admission->credential->producer;
	task->deadline = Clock::now() + options_.timeout;
	if (request.path == "/v2/open") {
		request_fields(d, {"expected_generation", "nonce"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Open;
		task->command.generation = number(field(d, "expected_generation"));
		task->command.nonce = identity(text(field(d, "nonce")));
	} else if (request.path == "/v2/close") {
		request_fields(d, {"generation"});
		task->kind = Task::Kind::Command;
		task->command.kind = Kind::Close;
		task->command.generation = number(field(d, "generation"));
	} else if (request.path == "/v2/transactions") {
		request_fields(d, {"generation", "sequence", "namespace", "expected_namespace", "operations"});
		task->kind = Task::Kind::Command;
		auto &c = task->command;
		c.generation = number(field(d, "generation"));
		c.sequence = number(field(d, "sequence"));
		c.name_space = text(field(d, "namespace"));
		c.expected_namespace = optional(d, "expected_namespace");
		check(task->admission->credential->namespaces.contains(c.name_space), "unauthorized namespace");
		auto &operations = field(d, "operations");
		check(operations.IsArray() && operations.Size() <= options_.node.application.mutations,
			  "operation count bound");
		for (const auto &item : operations.GetArray()) {
			fields(item, {"key", "value", "expected_revision", "absent_namespace_revision"});
			Mutation op;
			op.key = text(field(item, "key"));
			op.expected = optional(item, "expected_revision");
			op.absence_revision = optional(item, "absent_namespace_revision");
			auto &value = field(item, "value");
			if (!value.IsNull()) {
				auto parsed = detent::internal::parse_knob_def(value, op.key);
				check(parsed.has_value(), "invalid knob definition");
				op.value = std::move(parsed).value();
			}
			c.mutations.push_back(std::move(op));
		}
	} else if (request.path == "/v2/read") {
		request_fields(d, {"namespace", "keys"});
		task->kind = Task::Kind::Read;
		task->name_space = text(field(d, "namespace"));
		check(task->admission->credential->namespaces.contains(task->name_space), "unauthorized namespace");
		auto &keys = field(d, "keys");
		check(keys.IsArray() && keys.Size() <= options_.node.application.mutations, "read key count bound");
		std::set<std::string> unique;
		for (const auto &value : keys.GetArray()) {
			auto key = text(value);
			check(detent::is_valid_knob_name(key) && key.substr(0, key.find(':')) == task->name_space &&
					  unique.insert(key).second,
				  "invalid read key");
			task->keys.push_back(std::move(key));
		}
	} else if (request.path == "/v2/result") {
		request_fields(d, {"generation", "sequence"});
		task->kind = Task::Kind::Result;
		task->generation = number(field(d, "generation"));
		task->sequence = number(field(d, "sequence"));
	} else {
		return Reply{404, "{\"error\":\"unknown_endpoint\"}"};
	}
	if (task->kind == Task::Kind::Command) {
		(void)encode(task->command, options_.node.application);
	}
	{
		std::lock_guard lock(mutex_);
		if (stopping_) {
			return Reply{503, "{\"error\":\"stopping\"}"};
		}
		if (task->kind == Task::Kind::Command && !busy_producers_.insert(task->command.producer).second) {
			return Reply{429, "{\"error\":\"producer_busy\"}"};
		}
		try {
			queue_.push_back(task);
		} catch (...) {
			if (task->kind == Task::Kind::Command) {
				busy_producers_.erase(task->command.producer);
			}
			throw;
		}
	}
	return std::nullopt;
}

void Service::handle(const http::Request &request, http::ResponseWriter &response) {
	const auto context = options_.node.context;
	auto envelope = [context](Reply reply) {
		if (!reply.body.empty() && reply.body[0] == '{') {
			reply.body = "{\"authority\":{\"cluster\":\"" + hex(context.cluster) + "\",\"group\":\"" +
						 hex(context.group) + "\",\"epoch\":\"" + hex(context.epoch) + "\"}," +
						 reply.body.substr(1);
		}
		return reply;
	};
	std::shared_ptr<http::DeferredReply> deferred;
	std::function<void(Reply)> complete;
	Reply reply;
	try {
		if (request.path == "/v1/health" && request.method == "GET") {
			std::lock_guard lock(mutex_);
			reply = {200, health_};
		} else {
			auto admission =
				request.user_data ? std::static_pointer_cast<Admission>(request.user_data) : admit(request);
			http::DeferredOptions deferred_options;
			deferred_options.deadline = Clock::now() + options_.timeout;
			deferred_options.limits.body_bytes = options_.reply_bytes - 256;
			deferred_options.request_retention = admission;
			deferred = response.defer(std::move(deferred_options));
			if (deferred) {
				complete = [deferred, accounting = accounting_, limit = options_.reply_bytes,
							envelope](Reply value) {
					try {
						value = envelope(std::move(value));
						auto lease = std::make_shared<ReplyAdmission>();
						lease->accounting = accounting;
						// This endpoint emits only its fixed JSON content type and transport framing.
						if (value.body.size() > limit - 256) {
							deferred->abandon();
							return;
						}
						lease->bytes = value.body.size() + 256;
						{
							std::lock_guard lock(accounting->mutex);
							if (lease->bytes > limit - accounting->reply_bytes) {
								deferred->abandon();
								return;
							}
							accounting->reply_bytes += lease->bytes;
							lease->active = true;
						}
						http::Response owned;
						owned.status = value.status;
						owned.body = std::move(value.body);
						owned.content_type = "application/json";
						if (deferred->complete(std::move(owned), lease) != http::ReplySubmit::Accepted) {
							deferred->abandon();
						}
					} catch (...) {
						deferred->abandon();
					}
				};
				auto immediate = execute(request, std::move(admission), complete);
				if (immediate) {
					complete(std::move(*immediate));
				}
				return;
			}
			// Compatibility for synchronous recording/embedded writers. Production Asio
			// transfers response authority and never blocks on a per-request future.
			auto promise = std::make_shared<std::promise<Reply>>();
			auto future = promise->get_future();
			auto deadline = Clock::now() + options_.timeout;
			auto immediate = execute(request, std::move(admission),
									 [promise](Reply value) { promise->set_value(std::move(value)); });
			if (immediate) {
				reply = std::move(*immediate);
			} else if (future.wait_until(deadline) == std::future_status::ready) {
				reply = future.get();
			} else {
				reply = {503, "{\"error\":\"outcome_unknown\"}"};
			}
		}
	} catch (const std::invalid_argument &error) {
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		writer.String(error.what());
		reply = {400, "{\"error\":\"invalid_request\",\"detail\":" +
						  std::string(buffer.GetString(), buffer.GetSize()) + "}"};
	} catch (const std::exception &) {
		reply = {503, "{\"error\":\"outcome_unknown\"}"};
	}
	if (deferred) {
		if (complete) {
			complete(std::move(reply));
		} else {
			deferred->abandon();
		}
		return;
	}
	reply = envelope(std::move(reply));
	response.status(reply.status);
	response.set_header("Content-Type", "application/json");
	response.write(reply.body);
	response.end();
}

struct Service::Runtime {
	asio::io_context io{1};
	bool active = false;
	std::exception_ptr failure;
#if defined(__APPLE__) || defined(__FreeBSD__)
	std::shared_ptr<kronuz::journal::BsdCompletionQueue> queue =
		std::make_shared<kronuz::journal::BsdCompletionQueue>();
	std::shared_ptr<kronuz::journal::AsioCompletionDriver<kronuz::journal::BsdCompletionQueue>> driver =
		std::make_shared<kronuz::journal::AsioCompletionDriver<kronuz::journal::BsdCompletionQueue>>(
			io.get_executor(), queue);
#endif
	void dispatch(const std::shared_ptr<Node> &owner) {
		auto &node = *owner;
#if defined(__APPLE__) || defined(__FreeBSD__)
		if (auto operation = node.worker().take_io_operation()) {
			if (active) {
				throw std::logic_error("authority dispatched overlapping storage jobs");
			}
			active = true;
			try {
				asio::co_spawn(io, driver->run(operation),
							   [this, owner, operation](std::exception_ptr error) {
								   if (error) {
									   failure = error;
									   owner->worker().storage_operation_failed(
										   operation, "authority completion driver failed");
								   }
								   active = false;
							   });
			} catch (...) {
				active = false;
				node.worker().storage_operation_failed(operation, "authority completion dispatch failed");
				throw;
			}
		}
#else
		(void)node;
#endif
	}
	asio::awaitable<void> run(Service &service, std::promise<void> ready);
};
void Service::run(std::promise<void> ready) {
	try {
		Runtime runtime;
		asio::co_spawn(runtime.io, runtime.run(*this, std::move(ready)), [this](std::exception_ptr error) {
			if (error) {
				std::lock_guard lock(mutex_);
				stopping_ = true;
				health_ = std::string("{\"mode\":\"") +
						  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
						  "\",\"fenced\":true}";
			}
		});
		runtime.io.run();
	} catch (...) {
		// A backend/runtime failure before admission must settle the constructor.
		try {
			ready.set_exception(std::current_exception());
		} catch (const std::future_error &) {
		}
	}
}
asio::awaitable<void> Service::Runtime::run(Service &service, std::promise<void> ready) {
	auto &options_ = service.options_;
	auto &mutex_ = service.mutex_;
	auto &stopping_ = service.stopping_;
	auto &queue_ = service.queue_;
	auto &busy_producers_ = service.busy_producers_;
	auto &health_ = service.health_;
	auto accounting_ = service.accounting_;
	asio::steady_timer timer(io);
	std::shared_ptr<Node> owned_node;
	std::shared_ptr<transport::Peers> peers;

	std::map<std::uint64_t, std::vector<std::shared_ptr<Task>>> pending;
	std::uint64_t next = 0;
	bool started = false;
	std::uint64_t last_tick = 0;
	auto election_delay = [&] {
		std::uint32_t entropy = 0;
		check(RAND_bytes(reinterpret_cast<unsigned char *>(&entropy), sizeof(entropy)) == 1,
			  "election entropy");
		const auto &timing = options_.node.timing;
		return timing.election_min + entropy % (timing.election_max - timing.election_min + 1);
	};
	auto complete = [&](const std::shared_ptr<Task> &task, Reply reply) {
		auto callback = std::move(task->complete);
		if (task->kind == Task::Kind::Command) {
			std::lock_guard lock(mutex_);
			busy_producers_.erase(task->command.producer);
		}
		if (callback) {
			callback(std::move(reply));
		}
	};
	try {
		// Provision the parent directory explicitly; newly created final directory
		// receives a durable parent namespace barrier before Journal creation.
		if (std::filesystem::create_directory(options_.directory)) {
			check(::chmod(options_.directory.c_str(), 0700) == 0, "authority directory permissions");
			auto parent = options_.directory.parent_path();
			if (parent.empty()) {
				parent = ".";
			}
			int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
			if (fd < 0) {
				throw std::runtime_error("authority parent directory");
			}
			auto synced = ::fsync(fd);
			::close(fd);
			if (synced) {
				throw std::runtime_error("authority directory durability");
			}
		}
		auto node_options = options_.node;
#if defined(__APPLE__) || defined(__FreeBSD__)
		node_options.scheduling.async_storage = true;
#else
		check(!node_options.scheduling.async_storage && !node_options.scheduling.async_appends,
			  "native completion storage is unavailable on this platform");
#endif
		owned_node = std::make_shared<Node>(std::make_unique<kronuz::journal::PosixIO>(options_.directory),
											node_options);
		auto &node = *owned_node;
		if (std::filesystem::exists(options_.directory / "manifest")) {
			node.recover();
		} else {
			Identity id{};
			std::random_device random;
			for (char &byte : id) {
				byte = static_cast<char>(random());
			}
			if (id == Identity{}) {
				id[0] = 1;
			}
			node.create(id);
		}
		auto origin = Clock::now();
		while (!node.worker().ready()) {
			node.service({0, election_delay()});
			dispatch(owned_node);
			node.take_network_actions();
			if (node.worker().fenced()) {
				throw std::runtime_error("authority storage initialization fenced");
			}
			timer.expires_after(std::chrono::milliseconds(1));
			co_await timer.async_wait(asio::use_awaitable);
		}
		// Seed the first deadline before Start. Later Tick samples affect future
		// resets and cannot randomize a deadline that is already in force.
		node.service({0, election_delay()});
		node.start();
		if (options_.peers) {
			peers = std::make_shared<transport::Peers>(io.get_executor(), owned_node, node_options,
													   *options_.peers);
			peers->start();
		}
		check(!node.worker().fenced(), "authority committed application/profile mismatch");
		ready.set_value();
		started = true;
		for (;;) {
			{
				std::lock_guard lock(mutex_);
				if (stopping_) {
					break;
				}
			}
			auto now = Clock::now();
			auto tick = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - origin).count());
			// Fresh independent entropy avoids repeated split votes after partitions.
			last_tick = tick;
			if (peers)
				peers->admit();
			node.service({tick, election_delay()});
			dispatch(owned_node);
			if (failure) {
				std::rethrow_exception(failure);
			}
			for (auto &action : node.take_network_actions()) {
				if (std::holds_alternative<cluster::consensus::Send>(action)) {
					if (!peers)
						throw std::logic_error("single-voter authority cannot send");
					peers->send(std::move(std::get<cluster::consensus::Send>(action)));
				}
			}
			if (peers)
				peers->pump();
			for (auto &completion : node.take_completions()) {
				auto found = pending.find(completion.request);
				if (found == pending.end()) {
					continue;
				}
				for (std::size_t i = 0; i < found->second.size(); ++i) {
					auto &task = found->second[i];
					Reply reply;
					if (completion.rejection || node.worker().fenced()) {
						reply = {503, "{\"error\":\"outcome_unknown\"}"};
					} else if (task->kind == Task::Kind::Command) {
						auto r = completion.results.at(i);
						reply = {result_status(r), result_json(r)};
					} else if (task->kind == Task::Kind::Result) {
						auto r = node.state().lookup(task->admission->credential->producer, task->generation,
													 task->sequence);
						reply = {result_status(r), result_json(r)};
					} else {
						try {
							reply = {200, read_reply(node.state(), task->name_space, task->keys,
													 options_.reply_bytes - 512)};
						} catch (const std::length_error &) {
							reply = {413, "{\"error\":\"reply_bound\"}"};
						}
					}
					complete(task, std::move(reply));
				}
				pending.erase(found);
			}
			for (auto it = pending.begin(); it != pending.end();) {
				if (node.worker().fenced() ||
					std::all_of(it->second.begin(), it->second.end(),
								[&](auto &task) { return now >= task->deadline; })) {
					node.forget(it->first);
					for (auto &task : it->second) {
						complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
					}
					it = pending.erase(it);
				} else {
					++it;
				}
			}
			if (node.worker().fenced()) {
				throw std::runtime_error("authority worker fenced");
			}
			std::deque<std::shared_ptr<Task>> expired;
			{
				std::lock_guard lock(mutex_);
				while (!queue_.empty() && now >= queue_.front()->deadline) {
					expired.push_back(queue_.front());
					queue_.pop_front();
				}
			}
			for (auto &task : expired) {
				complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
			}
			std::vector<std::shared_ptr<Task>> selected;
			std::vector<Command> commands;
			{
				std::lock_guard lock(mutex_);
				if (!queue_.empty() &&
					(queue_.front()->kind != Task::Kind::Command || queue_.size() >= 64 ||
					 now >= queue_.front()->deadline - options_.timeout + std::chrono::milliseconds(5))) {
					std::size_t bytes = 16;
					while (!queue_.empty() && selected.size() < 64) {
						auto task = queue_.front();
						if (now >= task->deadline) {
							break;
						}
						if (!selected.empty() && (task->kind != Task::Kind::Command ||
												  selected.front()->kind != Task::Kind::Command)) {
							break;
						}
						auto size = task->kind == Task::Kind::Command
										? encode(task->command, options_.node.application).size() + 8
										: 0;
						if (size > options_.node.consensus.command_bytes - bytes) {
							break;
						}
						bytes += size;
						selected.push_back(task);
						if (task->kind == Task::Kind::Command) {
							commands.push_back(task->command);
						}
						queue_.pop_front();
					}
				}
			}
			if (!selected.empty()) {
				check(next < (std::uint64_t{1} << 63) - 1, "authority local request exhaustion");
				auto id = ++next;
				auto result = commands.empty() ? node.read(id) : node.propose(id, commands);
				if (result == cluster::consensus::SubmitResult::Accepted) {
					pending.emplace(id, std::move(selected));
				} else {
					std::lock_guard lock(mutex_);
					for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
						queue_.push_front(*it);
					}
				}
			}
			{
				std::lock_guard lock(mutex_);
				std::lock_guard account_lock(accounting_->mutex);
#if defined(__APPLE__) || defined(__FreeBSD__)
				auto io_stats = queue->stats();
				const std::string storage_health =
					",\"storage_async\":true,\"io_native_submitted\":" +
					std::to_string(io_stats.native_submitted) +
					",\"io_fallback_submitted\":" + std::to_string(io_stats.fallback_submitted);
#else
				const std::string storage_health = ",\"storage_async\":false";
#endif
				health_ = std::string("{\"mode\":\"") +
						  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
						  "\",\"ready\":" + std::string(node.worker().ready() ? "true" : "false") +
						  ",\"role\":" + std::to_string(static_cast<unsigned>(node.worker().role())) +
						  ",\"committed\":" + std::to_string(node.worker().committed()) +
						  ",\"term\":" + std::to_string(node.worker().term()) +
						  ",\"applied\":" + std::to_string(node.worker().applied_index()) +
						  ",\"revision\":" + std::to_string(node.state().revision()) +
						  ",\"requests\":" + std::to_string(accounting_->requests) +
						  ",\"reserved_body_bytes\":" + std::to_string(accounting_->body_bytes) +
						  ",\"reserved_reply_bytes\":" + std::to_string(accounting_->reply_bytes) +
						  storage_health +
						  ",\"peer_admitted\":" + std::to_string(peers ? peers->stats().admitted : 0) +
						  ",\"peer_dropped\":" + std::to_string(peers ? peers->stats().dropped : 0) +
						  ",\"checkpoint_busy\":" + (node.checkpoint_busy() ? "true" : "false") +
						  ",\"fenced\":" + (node.worker().fenced() ? "true" : "false") + "}";
			}
			timer.expires_after(std::chrono::milliseconds(1));
			co_await timer.async_wait(asio::use_awaitable);
		}
	} catch (...) {
		if (!started) {
			ready.set_exception(std::current_exception());
		} else {
			std::lock_guard lock(mutex_);
			stopping_ = true;
			health_ = std::string("{\"mode\":\"") +
					  (options_.peers ? "durable_fixed_voters" : "durable_single_voter") +
					  "\",\"fenced\":true}";
		}
	}
	if (peers)
		peers->stop();
	while (active || (peers && (!peers->drained() || !owned_node->channel().drained()))) {
		if (owned_node) {
			owned_node->service({last_tick, options_.node.timing.election_min});
			dispatch(owned_node);
			owned_node->take_network_actions();
		}
		timer.expires_after(std::chrono::milliseconds(1));
		co_await timer.async_wait(asio::use_awaitable);
	}

	for (auto &[id, tasks] : pending) {
		(void)id;
		for (auto &task : tasks) {
			complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
		}
	}
	std::deque<std::shared_ptr<Task>> remaining;
	{
		std::lock_guard lock(mutex_);
		remaining.swap(queue_);
	}
	for (auto &task : remaining) {
		complete(task, {503, "{\"error\":\"outcome_unknown\"}"});
	}
}
} // namespace detent::authority
