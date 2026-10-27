#include "detent_delivery/response.hh"
#include "detent_distribution/encoded_update.hh"
#include "detent_distribution/memory_resource.hh"
#include "response_probe_internal.hh"
#include <algorithm>
#include <limits>
#include <charconv>
#include <limits>
#include <cstring>
#include <exception>
#include <http_client_core.h>
#include <sha256.h>
namespace detent::delivery {
namespace d = distribution;
namespace client = http::client;
std::uint64_t maximum_response_wire_bytes(std::uint64_t logical_bytes) {
	const auto maximum = std::uint64_t(d::encoded_update_fragment_limit) * d::fragment_bytes;
	if (!logical_bytes || logical_bytes > maximum)
		throw std::invalid_argument("delivery payload exceeds fragment directory limit");
	const auto frames = logical_bytes / d::fragment_bytes + 1;
	const auto frame = d::OwnedFragmentEncoder::maximum_wire_bytes();
	const auto half = (std::numeric_limits<std::uint64_t>::max() - 65536) / 2;
	if (!frame || frames > half / frame)
		throw std::length_error("delivery HTTP wire bound overflow");
	return frames * frame * 2 + 65536;
}
namespace {
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}

client::Limits http_limits(std::uint64_t maximum) {
	client::Limits limits;
	limits.body_bytes = maximum;
	limits.wire_bytes = maximum;
	return limits;
}
client::ResponsePolicy http_policy() {
	client::ResponsePolicy policy;
	policy.require_http11 = true;
	policy.unique_headers = true;
	policy.informational = false;
	return policy;
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
	std::shared_ptr<std::pmr::memory_resource> resource;
	std::vector<std::pmr::string, d::StorageAllocator<std::pmr::string>> names;
	std::vector<std::string_view, d::StorageAllocator<std::string_view>> selected;
	std::uint64_t maximum_wire;
	client::ResponseParser parser;
	std::span<const char> body;
	std::size_t position = 0, size = 0, disposal = 0;
	std::uint64_t received = 0, logical_bytes = 0, wire_bytes = 0;
	class SHA256 payload_hash;
	std::array<unsigned char, 32> payload_digest{};
	bool probe = false, reference = false;
	bool compression, headers_done = false, http_done = false, complete = false, failed = false,
	                  taken = false, disposed = false;
	d::UpdateKind kind = d::UpdateKind::Snapshot;
	std::optional<d::FragmentDecoder> frames;
	std::optional<d::DecodedFragment> fragment;
	std::size_t fragment_position = 0;
	std::optional<d::SnapshotDecoder> snapshot;
	std::optional<d::DeltaDecoder> delta;
	std::shared_ptr<Decoded> decoded;
	Impl(d::Cursor configured, std::optional<d::Cursor> from, d::NamespaceSelection scope, bool coding,
	     d::Limits bounds, std::optional<SessionResponseBinding> binding)
	    : target(configured), base(from), session(std::move(binding)), limits(std::move(bounds)),
	      resource(allocators::make_charged_resource(limits.storage_account, limits.storage_allocate,
		                                             limits.storage_release)),
	      names(d::StorageAllocator<std::pmr::string>(limits.storage_account)),
	      selected(d::StorageAllocator<std::string_view>(limits.storage_account)),
	      maximum_wire(maximum_response_wire_bytes(limits.bytes)),
	      parser(http_limits(maximum_wire), http_policy(), allocators::AllocationContext(resource)),
	      body(parser.body()), compression(coding) {
		require(target.cluster != d::Identity{} && target.group != d::Identity{} &&
					target.restore_epoch != d::Identity{} && target.scope != d::ScopeIdentity{} &&
					limits.bytes &&
					scope.names.size() <= limits.namespaces,
				"invalid delivery response admission");
		if (session && session->purpose != SessionResponsePurpose::Open)
			require(session->session != d::Identity{} &&
						(session->purpose != SessionResponsePurpose::Next || session->window),
					"invalid session response binding");
		names.reserve(scope.names.size());
		selected.reserve(scope.names.size());
		for (auto name : scope.names)
			names.emplace_back(name, resource.get());
		for (const auto &name : names)
			selected.push_back(name);
		decoded = std::allocate_shared<Decoded>(d::StorageAllocator<Decoded>(limits.storage_account));
	}
	void release_body() {
		parser.consume_body(size);
		body = parser.body();
		position = size = 0;
	}
	std::string_view header(std::string_view key) const {
		auto found = parser.head().get(key);
		if (!found)
			throw std::invalid_argument("missing delivery response header: " + std::string(key) + " (HTTP " +
			                            std::to_string(parser.head().status) + ")");
		return *found;
	}

	void admit_response() {
		require(!parser.head().contains("trailer"), "delivery HTTP trailers unsupported");
		const auto status = parser.head().status;
		const bool update = status == 200 && (!session || session->purpose == SessionResponsePurpose::Next);
		if (!update) {
			require(parser.head().major == 1 && parser.head().minor == 1, "control HTTP version rejected");
			require((session && status == 200 && session->purpose == SessionResponsePurpose::Open) ||
			            (session && status == 204 && session->purpose != SessionResponsePurpose::Open) ||
			            status == 400 || status == 401 || status == 403 || status == 409 || status == 410 ||
			            status == 429 || status == 503,
			        "delivery control status rejected");
			require(!parser.head().contains("transfer-encoding"), "delivery control requires fixed framing");
			std::uint64_t length = 0;
			if (parser.head().contains("content-length"))
				length = number(header("content-length"));
			if (status == 204)
				require(!length, "session idle response has a body");
			else
				require(length && length <= 4096 && header("content-type") == "application/json",
				        "delivery control body bound or type");
			control = d::make_owned_buffer(limits.storage_account, static_cast<std::size_t>(length));
			maximum_wire = 65536 + 4096;
			headers_done = true;
			return;
		}
		if (session) {
			require(header("x-detent-session-version") == "1" &&
			            number(header("x-detent-window")) == session->window,
			        "session response window mismatch");
			std::array<char, 32> identity{};
			for (std::size_t i = 0; i < session->session.size(); ++i) {
				const auto byte = static_cast<unsigned char>(session->session[i]);
				identity[2 * i] = "0123456789abcdef"[byte >> 4];
				identity[2 * i + 1] = "0123456789abcdef"[byte & 15];
			}
			require(header("x-detent-session") == std::string_view(identity.data(), identity.size()),
			        "session response identity mismatch");
		}
		require(parser.head().status == 200 && parser.head().major == 1 && parser.head().minor == 1,
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
		if (parser.head().contains("transfer-encoding"))
			require(header("transfer-encoding") == "chunked" && !parser.head().contains("content-length"),
			        "delivery HTTP framing conflict");
		else
			require(number(header("content-length")) > 0 && number(header("content-length")) <= maximum_wire,
			        "delivery HTTP length bound");
		frames.emplace(kind, limits.bytes, compression, limits.storage_account);
		if (kind != d::UpdateKind::Snapshot)
			require(base && base->same_identity(target) && target.revision > base->revision,
			        "delivery delta base mismatch");
		if (!probe) {
			if (kind == d::UpdateKind::Snapshot)
				snapshot.emplace(target, d::NamespaceSelection(selected), limits);
			else
				delta.emplace(*base, d::NamespaceSelection(selected), limits, kind);
		}
		headers_done = true;
	}
	void advance(d::WorkBudget &budget) {
		if (control) {
			if (!budget.tokens || !budget.bytes)
				return;
			const auto n = std::min(size - position, budget.bytes);
			control->append({body.data() + position, n});
			position += n;
			budget.bytes -= n;
			--budget.tokens;
			if (position == size) {
				release_body();
				if (http_done) {
					control->seal_prefix();
					complete = true;
				}
			}
			return;
		}
		while (budget.bytes && budget.tokens && !complete) {
			if (fragment) {
				auto bytes = std::span<const char>(fragment->body->data() + fragment_position,
				                                   fragment->body->size() - fragment_position);
				const auto before = bytes.size();
				if (probe) {
					const auto n = std::min(bytes.size(), budget.bytes);
					if (n)
						payload_hash.add(bytes.data(), n);
					bytes = bytes.subspan(n);
					budget.bytes -= n;
					--budget.tokens;
					logical_bytes += n;
				} else if (snapshot)
					snapshot->feed(bytes, budget);
				else
					delta->feed(bytes, budget);
				if (reference) {
					const auto n = before - bytes.size();
					if (n)
						payload_hash.add(fragment->body->data() + fragment_position, n);
					logical_bytes += n;
				}
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
			release_body();
			if (!http_done)
				return;
			require(headers_done, "delivery response has no validated headers");
			frames->finish();
			if (probe)
				payload_hash.getHash(payload_digest.data());
			else if (snapshot) {
				decoded->snapshot.emplace(snapshot->finish());
				require(decoded->snapshot->cursor() == target, "delivery header and body cursor disagree");
			} else {
				decoded->delta.emplace(delta->finish());
				require(decoded->delta->target() == target, "delivery header and body cursor disagree");
			}
			if (reference)
				payload_hash.getHash(payload_digest.data());
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
		const auto n = std::min({input.size(), client::ResponseParser::read_bytes, budget.bytes});
		require(n <= p.maximum_wire - p.received, "delivery HTTP wire bound");
		const auto before = input.size();
		p.parser.feed(input, n);
		const auto consumed = before - input.size();
		budget.bytes -= consumed;
		--budget.tokens;
		p.received += consumed;
		if (p.parser.head_ready()) {
			p.admit_response();
			p.parser.accept_head();
		}
		p.body = p.parser.body();
		p.size = p.body.size();
		p.wire_bytes += p.size;
		p.http_done = p.parser.done();
		if (p.http_done)
			require(input.empty(), "trailing delivery HTTP bytes");
		p.advance(budget);
	} catch (const client::Error &error) {
		p.failed = true;
		if (error.kind() == client::ErrorKind::Pressure)
			throw d::StoragePressure(error.what());
		throw std::invalid_argument(error.what());
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
bool ResponseDecoder::reusable() const noexcept { return done() && impl_->parser.reusable(); }
ReceivedUpdate ResponseDecoder::take() {
	require(done() && !impl_->taken && !impl_->control && !impl_->probe && !impl_->reference,
	        "delivery update incomplete");
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
void detail::ProbeDecoderAccess::prepare(ResponseDecoder &decoder, bool reference) {
	auto &p = *decoder.impl_;
	require(!p.probe && !p.reference && !p.received && !p.headers_done && !p.taken && !p.failed &&
	            p.session && p.session->purpose == SessionResponsePurpose::Next,
	        "probe decoder requires a new session Next response");
	p.probe = !reference;
	p.reference = reference;
	if (p.probe)
		p.decoded.reset();
}
detail::ProbeOffer detail::ProbeDecoderAccess::take(ResponseDecoder &decoder) {
	auto &p = *decoder.impl_;
	require((p.probe || p.reference) && decoder.done() && !p.taken && !p.control && p.session,
	        "probe response incomplete");
	p.taken = true;
	return {p.session->window, p.target, p.kind, p.logical_bytes, p.wire_bytes, p.payload_digest};
}
bool ResponseDecoder::is_control() const noexcept { return static_cast<bool>(impl_->control); }
ReceivedControl ResponseDecoder::take_control() {
	require(done() && !impl_->taken && impl_->control, "session control incomplete");
	impl_->taken = true;
	return {impl_->parser.head().status, std::move(impl_->control)};
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
		if (!pay(p.control->storage_capacity()))
			return false;
		p.control.reset();
	}
	if (!p.parser.discard(budget.bytes, budget.tokens))
		return false;
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
	p.disposed = true;
	return true;
}
} // namespace detent::delivery
