#include "detent_distribution_http/session_request.hh"
#include "detent/json_schema_internal.hh"
#include "detent/validation.hh"
#include <rapidjson/document.h>

namespace detent::distribution_http {
namespace d = distribution;
namespace {
struct PageAllocator {
	static constexpr bool kNeedFree = false;
	d::PagedStorage *arena = nullptr;
	void *Malloc(std::size_t size) {
		if (!arena)
			throw std::logic_error("session parser allocator missing");
		return size ? arena->allocate(size) : nullptr;
	}
	void *Realloc(void *original, std::size_t old_size, std::size_t size) {
		auto replacement = Malloc(size);
		if (original)
			std::memcpy(replacement, original, std::min(old_size, size));
		return replacement;
	}
	static void Free(void *) noexcept {}
};
d::StorageLedger &checked_account(const std::shared_ptr<d::StorageLedger> &account) {
	if (!account)
		throw std::invalid_argument("session parser account missing");
	return *account;
}
using Document = rapidjson::GenericDocument<rapidjson::UTF8<>, PageAllocator, PageAllocator>;
using Value = Document::ValueType;
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
void fields(const Value &value, std::initializer_list<std::string_view> allowed) {
	require(value.IsObject() && value.MemberCount() <= allowed.size(), "session request object bound");
	for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
		std::string_view name(member->name.GetString(), member->name.GetStringLength());
		require(std::find(allowed.begin(), allowed.end(), name) != allowed.end(), "unknown session field");
		for (auto previous = value.MemberBegin(); previous != member; ++previous)
			require(name != std::string_view(previous->name.GetString(), previous->name.GetStringLength()),
					"duplicate session field");
	}
}
const Value &field(const Value &value, const char *name) {
	require(value.HasMember(name), "missing session field");
	return value[name];
}
std::string_view text(const Value &value, std::size_t maximum) {
	require(value.IsString() && value.GetStringLength() <= maximum, "session string bound");
	std::string_view result(value.GetString(), value.GetStringLength());
	require(result.find('\0') == std::string_view::npos, "NUL in session string");
	return result;
}
std::uint64_t number(const Value &value) {
	require(value.IsUint64(), "invalid session integer");
	return value.GetUint64();
}
template <class Identity> Identity identity(const Value &value) {
	Identity result{};
	auto bytes = text(value, result.size() * 2);
	require(bytes.size() == result.size() * 2, "session identity length");
	auto digit = [](char value) -> unsigned {
		if (value >= '0' && value <= '9')
			return value - '0';
		if (value >= 'a' && value <= 'f')
			return value - 'a' + 10;
		throw std::invalid_argument("session identity requires lowercase hexadecimal");
	};
	for (std::size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>((digit(bytes[2 * i]) << 4) | digit(bytes[2 * i + 1]));
	require(result != Identity{}, "zero session identity");
	return result;
}
d::Cursor cursor(const Value &value) {
	fields(value, {"cluster", "group", "epoch", "scope", "revision"});
	return {identity<d::Identity>(field(value, "cluster")), identity<d::Identity>(field(value, "group")),
			identity<d::Identity>(field(value, "epoch")), identity<d::ScopeIdentity>(field(value, "scope")),
			number(field(value, "revision"))};
}
} // namespace
std::optional<RequestKind> classify_session_route(std::string_view path) noexcept {
	if (path == "/v2/distribution/session/open")
		return RequestKind::Open;
	if (path == "/v2/distribution/session/next")
		return RequestKind::Next;
	if (path == "/v2/distribution/session/close")
		return RequestKind::Close;
	return {};
}
struct ParsedSessionRequest::Impl {
	std::shared_ptr<d::StorageLedger> account;
	std::shared_ptr<const d::OwnedBuffer> input;
	RequestLimits limits;
	d::PagedStorage arena;
	PageAllocator allocator;
	Document document;
	SessionRequest result;
	bool parsed = false, retiring = false;
	Impl(std::shared_ptr<d::StorageLedger> ledger, RequestKind kind,
		 std::shared_ptr<const d::OwnedBuffer> body, RequestLimits bounds)
		: account(std::move(ledger)), input(std::move(body)), limits(bounds),
		  arena(checked_account(account), 4096, std::malloc, std::free,
				std::min<std::size_t>(limits.bytes, 1024 * 1024) * 32 + 8192),
		  allocator{&arena}, document(&allocator, 1024, &allocator), result{kind} {
		require((!input || input->sealed()) && limits.bytes && limits.bytes <= 1024 * 1024 &&
					limits.namespaces <= 4096 && limits.maximum_wait_ms <= 60000,
				"invalid session parser admission");
	}
	void parse(std::string_view bytes) {
		require(!parsed && !retiring, "session parser already consumed");
		parsed = true;
		require(bytes.size() <= limits.bytes && bytes.find('\0') == std::string_view::npos &&
					!detent::internal::nesting_depth_exceeds(bytes, 64),
				"session JSON bound");
		document.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(),
																							   bytes.size());
		require(!document.HasParseError() && document.IsObject(), "invalid session JSON");
		if (result.kind != RequestKind::Distribution)
			require(number(field(document, "version")) == 1, "unsupported session version");
		if (result.kind == RequestKind::Distribution || result.kind == RequestKind::Open) {
			if (result.kind == RequestKind::Open)
				fields(document, {"version", "cluster", "group", "epoch", "scope", "namespaces", "base",
								  "coding", "profile", "force_snapshot"});
			else
				fields(document,
					   {"cluster", "group", "epoch", "scope", "namespaces", "base", "coding", "profile"});
			result.identity = {identity<d::Identity>(field(document, "cluster")),
							   identity<d::Identity>(field(document, "group")),
							   identity<d::Identity>(field(document, "epoch")),
							   identity<d::ScopeIdentity>(field(document, "scope")), 0};
			require(number(field(document, "profile")) == 1, "unsupported session profile");
			auto coding = text(field(document, "coding"), 4);
			require(coding == "raw" || coding == "zstd", "unsupported session coding");
			result.compressed = coding == "zstd";
			const auto &names = field(document, "namespaces");
			require(names.IsArray() && names.Size() <= limits.namespaces, "session namespace bound");
			auto directory =
				static_cast<std::string_view *>(arena.allocate(names.Size() * sizeof(std::string_view)));
			std::size_t size = 0;
			for (const auto &value : names.GetArray()) {
				auto name = text(value, 198);
				require(!name.empty() && std::all_of(name.begin(), name.end(), detent::detail::is_name_char),
						"invalid session namespace");
				std::construct_at(directory + size++, name);
			}
			std::sort(directory, directory + size);
			size = std::unique(directory, directory + size) - directory;
			result.namespaces = {directory, size};
			if (document.HasMember("base") && !field(document, "base").IsNull())
				result.base = cursor(field(document, "base"));
			if (document.HasMember("force_snapshot")) {
				require(field(document, "force_snapshot").IsBool(), "invalid session snapshot selection");
				result.force_snapshot = field(document, "force_snapshot").GetBool();
			}
		} else {
			result.session = identity<d::Identity>(field(document, "session"));
			if (result.kind == RequestKind::Close)
				fields(document, {"version", "session"});
			else {
				require(result.kind == RequestKind::Next, "invalid session request kind");
				fields(document, {"version", "session", "after", "published", "wait_ms"});
				result.after = number(field(document, "after"));
				result.wait_ms = document.HasMember("wait_ms") ? number(field(document, "wait_ms"))
															   : limits.maximum_wait_ms;
				require(result.wait_ms <= limits.maximum_wait_ms, "session wait bound");
				if (document.HasMember("published"))
					result.published = cursor(field(document, "published"));
			}
		}
	}
};
ParsedSessionRequest::ParsedSessionRequest(std::shared_ptr<d::StorageLedger> account, RequestKind kind,
										   std::shared_ptr<const d::OwnedBuffer> input, RequestLimits limits)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, account, kind, std::move(input),
										limits)) {}
ParsedSessionRequest::~ParsedSessionRequest() = default;
const SessionRequest &ParsedSessionRequest::request() const noexcept { return impl_->result; }
std::size_t ParsedSessionRequest::storage_capacity() const noexcept { return impl_->arena.used(); }
void ParsedSessionRequest::parse() {
	require(static_cast<bool>(impl_->input), "session parser input missing");
	impl_->parse(impl_->input->view());
}
void ParsedSessionRequest::parse(std::string_view bytes) { impl_->parse(bytes); }
bool ParsedSessionRequest::retire_step() noexcept {
	if (!impl_->retiring) {
		impl_->retiring = true;
		impl_->document.SetNull();
		impl_->input.reset();
	}
	return impl_->arena.release_one();
}
d::RetirementHandle<ParsedSessionRequest> parse_session_request(d::RetirementDomain &domain, RequestKind kind,
																std::shared_ptr<const d::OwnedBuffer> input,
																RequestLimits limits) {
	auto result = domain.make<ParsedSessionRequest>(domain.storage_account(), kind, std::move(input), limits);
	result->parse();
	return result;
}
d::StorageOwner<ParsedSessionRequest>
parse_session_request_on_worker(std::shared_ptr<d::StorageLedger> account, RequestKind kind,
								std::string_view bytes, RequestLimits limits) {
	auto result = d::make_storage_owner<ParsedSessionRequest>(account, std::malloc, std::free, account, kind,
															  nullptr, limits);
	result->parse(bytes);
	return result;
}
} // namespace detent::distribution_http
