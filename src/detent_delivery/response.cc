#include "detent_delivery/response.hh"
#include "detent_distribution/memory_resource.hh"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <exception>
#include <http_parser.h>
#include <map>
namespace detent::delivery {
namespace d = distribution;
namespace {
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
std::uint64_t number(std::string_view value) {
	std::uint64_t result = 0;
	auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	require(!value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
			"invalid delivery response integer");
	return result;
}
struct Decoded {
	std::optional<d::ValidatedSnapshot> snapshot;
	std::optional<d::ValidatedDelta> delta;
};
} // namespace
struct ResponseDecoder::Impl {
	d::Cursor target;
	std::optional<d::Cursor> base;
	std::optional<SessionResponseBinding> session;
	std::shared_ptr<d::OwnedBuffer> control;
	d::Limits limits;
	d::LedgerMemoryResource resource;
	std::vector<std::pmr::string, d::StorageAllocator<std::pmr::string>> names;
	std::vector<std::string_view, d::StorageAllocator<std::string_view>> selected;
	std::pmr::map<std::pmr::string, std::pmr::string, std::less<>> headers;
	std::pmr::string field, value;
	http_parser parser{};
	http_parser_settings settings{};
	std::array<char, 4096> body{};
	std::size_t position = 0, size = 0, header_bytes = 0, disposal = 0;
	std::uint64_t received = 0, maximum_wire;
	bool compression, last_value = false, headers_done = false, http_done = false, complete = false,
					  failed = false, taken = false, disposed = false;
	d::UpdateKind kind = d::UpdateKind::Snapshot;
	std::exception_ptr error;
	std::optional<d::FragmentDecoder> frames;
	std::optional<d::DecodedFragment> fragment;
	std::size_t fragment_position = 0;
	std::optional<d::SnapshotDecoder> snapshot;
	std::optional<d::DeltaDecoder> delta;
	std::shared_ptr<Decoded> decoded;
	Impl(d::Cursor configured, std::optional<d::Cursor> from, d::NamespaceSelection scope, bool coding,
		 d::Limits bounds, std::optional<SessionResponseBinding> binding)
		: target(configured), base(from), session(std::move(binding)), limits(std::move(bounds)), resource(limits.storage_account),
		  names(d::StorageAllocator<std::pmr::string>(limits.storage_account)),
		  selected(d::StorageAllocator<std::string_view>(limits.storage_account)), headers(&resource),
		  field(&resource), value(&resource), compression(coding) {
		require(target.cluster != d::Identity{} && target.group != d::Identity{} &&
					target.restore_epoch != d::Identity{} && target.scope != d::ScopeIdentity{} &&
					limits.bytes && limits.bytes <= 1024ull * 1024 * 1024 &&
					scope.names.size() <= limits.namespaces,
				"invalid delivery response admission");
		if (session && session->purpose != SessionResponsePurpose::Open)
			require(session->session != d::Identity{} &&
				(session->purpose != SessionResponsePurpose::Next || session->window), "invalid session response binding");
		maximum_wire = (limits.bytes / d::fragment_bytes + 1) * d::OwnedFragmentEncoder::maximum_wire_bytes();
		// Permit bounded HTTP chunk overhead without accumulating the response.
		maximum_wire = maximum_wire * 2 + 65536;
		names.reserve(scope.names.size());
		selected.reserve(scope.names.size());
		for (auto name : scope.names)
			names.emplace_back(name, &resource);
		for (const auto &name : names)
			selected.push_back(name);
		decoded = std::allocate_shared<Decoded>(d::StorageAllocator<Decoded>(limits.storage_account));
		http_parser_init(&parser, HTTP_RESPONSE);
		parser.data = this;
		settings.on_header_field = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) {
				require(!self.headers_done, "delivery HTTP trailers unsupported");
				if (self.last_value)
					self.commit_header();
				self.admit_header(n);
				for (std::size_t i = 0; i < n; ++i) {
					const auto c = bytes[i];
					self.field += c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
				}
			});
		};
		settings.on_header_value = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) {
				self.admit_header(n);
				self.last_value = true;
				self.value.append(bytes, n);
			});
		};
		settings.on_headers_complete = [](http_parser *p) {
			return callback(p, [](Impl &self) { self.admit_response(); });
		};
		settings.on_body = [](http_parser *p, const char *bytes, std::size_t n) {
			return callback(p, [&](Impl &self) {
				require(self.headers_done && n <= self.body.size() - self.size,
						"delivery body intake overflow");
				std::memcpy(self.body.data() + self.size, bytes, n);
				self.size += n;
			});
		};
		settings.on_message_complete = [](http_parser *p) {
			auto &self = *static_cast<Impl *>(p->data);
			self.http_done = true;
			http_parser_pause(p, 1);
			return 0;
		};
	}
	template <class F> static int callback(http_parser *parser, F &&work) noexcept {
		auto &self = *static_cast<Impl *>(parser->data);
		try {
			work(self);
			return 0;
		} catch (...) {
			if (!self.error)
				self.error = std::current_exception();
			return 1;
		}
	}
	void admit_header(std::size_t n) {
		require(n <= 65536 - header_bytes, "delivery response header bound");
		header_bytes += n;
	}
	void commit_header() {
		require(!field.empty() && headers.size() < 128 && headers.emplace(field, value).second,
				"duplicate or excessive delivery response headers");
		field.clear();
		value.clear();
		last_value = false;
	}
	std::string_view header(std::string_view key) const {
		auto found = headers.find(key);
		require(found != headers.end(), "missing delivery response header");
		return found->second;
	}
	void admit_response() {
		if (!field.empty())
			commit_header();
		require(!headers.contains("trailer"), "delivery HTTP trailers unsupported");
		if (session) {
			require(parser.http_major == 1 && parser.http_minor == 1, "session HTTP version rejected");
			const bool update = parser.status_code == 200 && session->purpose == SessionResponsePurpose::Next;
			if (!update) {
				const auto status = parser.status_code;
				require((status == 200 && session->purpose == SessionResponsePurpose::Open) ||
					(status == 204 && session->purpose != SessionResponsePurpose::Open) || status == 400 ||
					status == 401 || status == 403 || status == 409 || status == 410 || status == 429 || status == 503,
					"session HTTP status rejected");
				require(!headers.contains("transfer-encoding"), "session control requires fixed framing");
				std::uint64_t length = 0;
				if (headers.contains("content-length")) length = number(header("content-length"));
				if (status == 204) require(!length, "session idle response has a body");
				else require(length && length <= 4096 && header("content-type") == "application/json",
					"session control body bound or type");
				control = d::make_owned_buffer(limits.storage_account, static_cast<std::size_t>(length));
				maximum_wire = 65536 + 4096;
				headers_done = true;
				return;
			}
			require(header("x-detent-session-version") == "1" &&
				number(header("x-detent-window")) == session->window, "session response window mismatch");
			std::array<char, 32> identity{};
			for (std::size_t i = 0; i < session->session.size(); ++i) {
				const auto byte = static_cast<unsigned char>(session->session[i]);
				identity[2 * i] = "0123456789abcdef"[byte >> 4];
				identity[2 * i + 1] = "0123456789abcdef"[byte & 15];
			}
			require(header("x-detent-session") == std::string_view(identity.data(), identity.size()),
				"session response identity mismatch");
		}
		require(parser.status_code == 200 && parser.http_major == 1 && parser.http_minor == 1,
				"delivery HTTP status or version rejected");
		require(header("content-type") == "application/vnd.detent.distribution" &&
					header("x-detent-profile") == "1" &&
					header("x-detent-coding") == (compression ? "zstd" : "raw"),
				"delivery response negotiation mismatch");
		const auto type = number(header("x-detent-kind"));
		require(type >= 1 && type <= 3, "unknown delivery update kind");
		kind = static_cast<d::UpdateKind>(type);
		if (session && kind == d::UpdateKind::Snapshot)
			require(session->snapshot_allowed, "unexpected session snapshot");
		target.revision = number(header("x-detent-revision"));
		if (base && base->same_identity(target))
			require(target.revision >= base->revision, "stale delivery response");
		if (headers.contains("transfer-encoding"))
			require(header("transfer-encoding") == "chunked" && !headers.contains("content-length"),
					"delivery HTTP framing conflict");
		else
			require(number(header("content-length")) > 0 && number(header("content-length")) <= maximum_wire,
					"delivery HTTP length bound");
		frames.emplace(kind, limits.bytes, compression, limits.storage_account);
		if (kind == d::UpdateKind::Snapshot)
			snapshot.emplace(target, d::NamespaceSelection(selected), limits);
		else {
			require(base && base->same_identity(target) && target.revision > base->revision,
					"delivery delta base mismatch");
			delta.emplace(*base, d::NamespaceSelection(selected), limits, kind);
		}
		headers_done = true;
	}
	void advance(d::WorkBudget &budget) {
		if (control) {
			if (!budget.tokens || !budget.bytes) return;
			const auto n = std::min(size - position, budget.bytes);
			control->append({body.data() + position, n});
			position += n;
			budget.bytes -= n;
			--budget.tokens;
			if (position == size) {
				position = size = 0;
				if (http_done) { control->seal_prefix(); complete = true; }
			}
			return;
		}
		while (budget.bytes && budget.tokens && !complete) {
			if (fragment) {
				auto bytes = std::span<const char>(fragment->body->data() + fragment_position,
												   fragment->body->size() - fragment_position);
				const auto before = bytes.size();
				if (snapshot)
					snapshot->feed(bytes, budget);
				else
					delta->feed(bytes, budget);
				fragment_position += before - bytes.size();
				if (!bytes.empty())
					return;
				fragment.reset();
				fragment_position = 0;
			}
			if (position < size) {
				auto bytes = std::span<const char>(body.data() + position, size - position);
				const auto before = bytes.size();
				frames->feed(bytes, budget);
				position += before - bytes.size();
				fragment = frames->take();
				if (fragment) {
					require(fragment->compressed == compression, "delivery fragment coding mismatch");
					continue;
				}
				if (position != size)
					return;
			}
			position = size = 0;
			if (!http_done)
				return;
			require(headers_done, "delivery response has no validated headers");
			frames->finish();
			if (snapshot)
				decoded->snapshot.emplace(snapshot->finish());
			else {
				decoded->delta.emplace(delta->finish());
				require(decoded->delta->target() == target, "delivery header and body cursor disagree");
			}
			complete = true;
		}
	}
};
ResponseDecoder::ResponseDecoder(d::Cursor configured, std::optional<d::Cursor> base,
								 d::NamespaceSelection names, bool compression, d::Limits limits,
								 std::optional<SessionResponseBinding> binding)
	: impl_([&] {
		  limits = d::detail::admitted_limits(std::move(limits));
		  return d::make_storage_owner<Impl>(limits.storage_account, limits.storage_allocate,
											 limits.storage_release, configured, base, names, compression,
											 limits, std::move(binding));
	  }()) {}
ResponseDecoder::~ResponseDecoder() = default;
void ResponseDecoder::feed(std::span<const char> &input, d::WorkBudget &budget) {
	auto &p = *impl_;
	require(!p.failed && !p.taken, "delivery response decoder unavailable");
	try {
		p.advance(budget);
		if (p.http_done) {
			require(input.empty(), "trailing delivery HTTP bytes");
			return;
		}
		if (p.fragment || p.size || input.empty() || !budget.tokens || !budget.bytes)
			return;
		const auto n = std::min({input.size(), p.body.size(), budget.bytes});
		require(n <= p.maximum_wire - p.received, "delivery HTTP wire bound");
		const auto consumed = http_parser_execute(&p.parser, &p.settings, input.data(), n);
		input = input.subspan(consumed);
		budget.bytes -= consumed;
		--budget.tokens;
		p.received += consumed;
		if (p.error)
			std::rethrow_exception(p.error);
		require(consumed == n &&
					(HTTP_PARSER_ERRNO(&p.parser) == HPE_OK || HTTP_PARSER_ERRNO(&p.parser) == HPE_PAUSED),
				"invalid delivery HTTP framing");
		if (p.http_done)
			require(input.empty(), "trailing delivery HTTP bytes");
		p.advance(budget);
	} catch (...) {
		p.failed = true;
		throw;
	}
}
void ResponseDecoder::step(d::WorkBudget &budget) {
	auto empty = std::span<const char>();
	feed(empty, budget);
}
bool ResponseDecoder::needs_input() const noexcept {
	return !impl_->http_done && !impl_->fragment && !impl_->size && !impl_->failed;
}
bool ResponseDecoder::done() const noexcept { return impl_->complete && !impl_->failed; }
ReceivedUpdate ResponseDecoder::take() {
	require(done() && !impl_->taken && !impl_->control, "delivery update incomplete");
	auto &p = *impl_;
	ReceivedUpdate result{p.kind, p.target, {}, {}};
	if (p.decoded->snapshot)
		result.snapshot = {p.decoded, &*p.decoded->snapshot};
	else
		result.delta = {p.decoded, &*p.decoded->delta};
	p.decoded.reset();
	p.taken = true;
	return result;
}
bool ResponseDecoder::is_control() const noexcept { return static_cast<bool>(impl_->control); }
ReceivedControl ResponseDecoder::take_control() {
	require(done() && !impl_->taken && impl_->control, "session control incomplete");
	impl_->taken = true;
	return {impl_->parser.status_code, std::move(impl_->control)};
}
void ResponseDecoder::eof() {
	if (!impl_->http_done || impl_->failed) {
		impl_->failed = true;
		throw std::invalid_argument("truncated delivery HTTP response");
	}
}
bool ResponseDecoder::discard(d::WorkBudget &budget) noexcept {
	auto &p = *impl_;
	p.failed = true;
	if (p.disposed)
		return true;
	if (!budget.bytes || !budget.tokens)
		return false;
	if (p.fragment) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = p.fragment->body->storage_capacity();
		const auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		p.fragment.reset();
	}
	try {
		if (p.snapshot && !p.snapshot->discard(budget))
			return false;
		p.snapshot.reset();
		if (p.delta && !p.delta->discard(budget))
			return false;
		p.delta.reset();
		if (p.decoded && p.decoded->snapshot && !p.decoded->snapshot->discard(budget))
			return false;
		if (p.decoded && p.decoded->delta && !p.decoded->delta->discard(budget))
			return false;
		p.decoded.reset();
	} catch (...) {
		return false;
	}
	if (p.frames && !p.frames->discard(budget))
		return false;
	p.frames.reset();
	auto pay = [&](std::size_t bytes) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = bytes;
		const auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		return true;
	};
	if (p.control) {
		if (!pay(p.control->storage_capacity())) return false;
		p.control.reset();
	}
	while (!p.headers.empty()) {
		const auto first = p.headers.begin();
		if (!pay(first->first.capacity() + first->second.capacity() + sizeof(*first) + 128))
			return false;
		p.headers.erase(first);
	}
	while (!p.names.empty()) {
		if (!pay(p.names.back().capacity() + sizeof(std::pmr::string)))
			return false;
		p.names.pop_back();
	}
	if (p.names.capacity()) {
		if (!pay(p.names.capacity() * sizeof(std::pmr::string)))
			return false;
		decltype(p.names) empty(p.names.get_allocator());
		p.names.swap(empty);
	}
	if (p.selected.capacity()) {
		if (!pay(p.selected.capacity() * sizeof(std::string_view)))
			return false;
		decltype(p.selected) empty(p.selected.get_allocator());
		p.selected.swap(empty);
	}
	// Release parser accumulation capacity once, including a cancelled partial header.
	if (!p.field.empty() || !p.value.empty() || p.field.capacity() > 15 || p.value.capacity() > 15) {
		if (!pay(p.field.capacity() + p.value.capacity()))
			return false;
		std::pmr::string field(&p.resource), value(&p.resource);
		p.field.swap(field);
		p.value.swap(value);
	}
	p.disposed = true;
	return true;
}
} // namespace detent::delivery
