// The Python binding: a thin pybind11 wrapper around detent::Client -- get()/get_bool()
// carry the exact same precedence chain and strict
// parsing the C++ API and detent-agent's CLI both use, all reused
// unchanged, nothing reimplemented here. Errors (missing knob, invalid name,
// a get_bool() value other than 0 or 1) raise detent.DetentError rather than returning a Result
// object, matching normal Python exception idiom instead of *_or_panic's
// process-abort-on-error C++ convention.
//
// Scope: file-source only (baked defaults, optionally a polled local
// snapshot file) -- the same scope as examples/demo.cc. A Python
// process that needs managed updates should run `detent-agent` alongside
// it and read via the shared-memory channel (see bindings/rust for the
// analogous case).
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "detent/client.hh"

#include <stdexcept>

namespace py = pybind11;

namespace {

// Thrown by every wrapper below on a detent::KnobError; translated to
// Python's detent.DetentError by py::register_exception (see PYBIND11_MODULE).
struct DetentError : std::runtime_error {
	explicit DetentError(const detent::KnobError& e) : std::runtime_error(e.what()) {}
};

template <typename T>
T unwrap(detent::Result<T, detent::KnobError> result) {
	if (!result) throw DetentError(result.error());
	return result.value();
}

} // namespace

PYBIND11_MODULE(detent, m) {
	m.doc() = "Detent: a runtime configuration (feature flag / kill switch) client.";

	py::register_exception<DetentError>(m, "DetentError");

	py::class_<detent::Client>(m, "Client")
		.def(py::init([](std::string defaults_path, std::optional<std::string> source_path,
		                  std::vector<std::string> cli_overrides, int poll_interval_ms) {
			     detent::InitOptions opts;
			     opts.defaults_path = std::move(defaults_path);
			     if (source_path) opts.source_path = *source_path;
			     opts.cli_overrides = std::move(cli_overrides);
			     opts.poll_interval = std::chrono::milliseconds{poll_interval_ms};
			     try {
				     return std::make_unique<detent::Client>(std::move(opts));
			     } catch (const std::exception& e) {
				     // detent::Client's constructor throws plain
				     // std::runtime_error (the fail-fast contract),
				     // not a detent::KnobError -- pybind11 would otherwise
				     // translate that into a generic Python RuntimeError
				     // instead of detent.DetentError, forcing callers to
				     // catch two different exception types for what's
				     // conceptually the same "detent rejected this" family
				     // of failures.
				     throw DetentError(detent::KnobError{detent::ErrorCategory::Uninitialized, "", e.what()});
			     }
		     }),
		     py::arg("defaults_path"), py::arg("source_path") = py::none(),
		     py::arg("cli_overrides") = std::vector<std::string>{}, py::arg("poll_interval_ms") = 200,
		     "Throws DetentError on invalid/missing defaults or CLI overrides "
		     "(fail-fast at startup).")
		.def(
			"get",
			[](const detent::Client& c, std::string_view name, std::optional<std::string> hash_val,
			   std::optional<std::string> switch_val) {
				return unwrap(c.get(name, hash_val, switch_val));
			},
			py::arg("name"), py::arg("hash_val") = py::none(), py::arg("switch_val") = py::none(),
			"Reads a knob's int64 value. Precedence: CLI override -> switch_val "
			"group match (forced value, or rollout_value for a hash inside the group's "
			"percentage) -> rollout_pct (rollout_value for a hash inside it) -> default. "
			"Raises DetentError if missing or invalid-name.")
		.def(
			"get_bool",
			[](const detent::Client& c, std::string_view name, std::optional<std::string> hash_val,
			   std::optional<std::string> switch_val) {
				return unwrap(c.get_as<bool>(name, hash_val, switch_val));
			},
			py::arg("name"), py::arg("hash_val") = py::none(), py::arg("switch_val") = py::none(),
			"get(), then 0 is False and 1 is True; any other value raises DetentError "
			"(ConversionFailed).")
		.def(
			"health",
			[](const detent::Client& c) {
				auto h = c.health();
				py::dict d;
				d["initialized"] = h.initialized;
				d["source_connected"] = h.source_connected;
				d["accepted_version"] = h.accepted_version;
				d["staleness_seconds"] = h.staleness_seconds;
				d["knobs"] = h.knobs;
				d["cli_overrides"] = h.cli_overrides;
				return d;
			},
			"Diagnostic snapshot, as a dict.");
}
