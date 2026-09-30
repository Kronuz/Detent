#include "shim.hh"

// HealthInfo's full generated definition (shim.hh only forward-declares it,
// see shim.hh's header comment for why); shim.cc is a normal leaf
// translation unit, so including the generated header directly here is not
// circular the way it would be from within shim.hh itself.
#include "detent-rust/src/lib.rs.h"

#include <stdexcept>

namespace detent_rust {

namespace {
// rust::Str's `explicit operator std::string_view()` is gated behind
// `#if __cplusplus >= 201703L` inside cxx's own runtime source (cxx.cc);
// that runtime is compiled by the separate `cxx` crate's own build script,
// which does not inherit this crate's `-std=c++20` build.rs setting, so
// the operator's compiled definition is missing at link time even though
// it's visible in the header -- a real, confirmed cxx-crate build-script
// quirk, not a typo. `.data()`/`.size()` have no such guard and are always
// linkable, so build the view from those directly instead.
std::string_view view(rust::Str s) { return std::string_view(s.data(), s.size()); }
} // namespace

std::unique_ptr<ClientHandle> new_client(
	rust::Str defaults_path, bool has_source_path, rust::Str source_path,
	rust::Vec<rust::String> cli_overrides, std::int32_t poll_interval_ms) {
	detent::InitOptions opts;
	opts.defaults_path = view(defaults_path);
	if (has_source_path) opts.source_path = view(source_path);
	opts.poll_interval = std::chrono::milliseconds{poll_interval_ms};
	for (const auto& o : cli_overrides) opts.cli_overrides.emplace_back(view(o));
	// detent::Client's constructor throws std::runtime_error on invalid/
	// missing defaults or CLI overrides (Section 9's fail-fast contract);
	// cxx translates that into Rust's Result::Err automatically because
	// this function's bridge declaration returns Result<UniquePtr<...>>.
	return std::make_unique<ClientHandle>(std::move(opts));
}

bool eval(const ClientHandle& client, rust::Str name, bool has_hash, rust::Str hash_val, bool has_switch,
          rust::Str switch_val) {
	std::optional<std::string_view> hv;
	std::optional<std::string_view> sv;
	if (has_hash) hv = view(hash_val);
	if (has_switch) sv = view(switch_val);
	auto result = client.client_.eval(view(name), hv, sv);
	if (!result) throw std::runtime_error(result.error().what());
	return result.value();
}

std::int64_t get_int(const ClientHandle& client, rust::Str name, bool has_switch, rust::Str switch_val) {
	std::optional<std::string_view> sv;
	if (has_switch) sv = view(switch_val);
	auto result = client.client_.get(view(name), sv);
	if (!result) throw std::runtime_error(result.error().what());
	return result.value();
}

HealthInfo health(const ClientHandle& client) noexcept {
	auto h = client.client_.health();
	HealthInfo out;
	out.initialized = h.initialized;
	out.source_connected = h.source_connected;
	out.accepted_version = h.accepted_version;
	out.staleness_seconds = h.staleness_seconds;
	out.bool_knobs = h.bool_knobs;
	out.int_knobs = h.int_knobs;
	out.cli_overrides = h.cli_overrides;
	return out;
}

} // namespace detent_rust
