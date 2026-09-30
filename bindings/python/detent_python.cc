// Section 5's "API surface (bindings and injection)" made concrete for
// Python: a thin pybind11 wrapper around detent::Client -- eval()/get()
// carry the exact same Section 8 precedence chain and Section 4 strict
// parsing the C++ API and detent-agent's CLI both use, all reused
// unchanged, nothing reimplemented here. Errors (missing knob, wrong type,
// invalid name) raise detent.DetentError rather than returning a Result
// object, matching normal Python exception idiom instead of *_or_panic's
// process-abort-on-error C++ convention.
//
// Scope: file-source only (baked defaults, optionally a polled local
// snapshot file) -- the same scope as examples/demo.cc. HTTP-source
// polling is not wired into this binding to avoid pulling detent_http (and
// its own POSIX-socket dependency) into every Python install; a Python
// process that needs live HTTP-distributed updates should run
// `detent-agent` alongside it and read via the shared-memory channel
// instead (see bindings/rust for the analogous case).
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
				     // std::runtime_error (Section 9's fail-fast contract),
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
		     "(Section 9: fail-fast at startup).")
		.def(
			"eval",
			[](const detent::Client& c, std::string_view name, std::optional<std::string> hash_val,
			   std::optional<std::string> switch_val) {
				return unwrap(c.eval(name, hash_val, switch_val));
			},
			py::arg("name"), py::arg("hash_val") = py::none(), py::arg("switch_val") = py::none(),
			"Evaluates a bool knob. Section 8 precedence: CLI override -> switch_val "
			"group match -> rollout_pct hash gate (if rollout_pct is set and hash_val "
			"given) -> default. Raises DetentError if missing/wrong-type/invalid-name.")
		.def(
			"get",
			[](const detent::Client& c, std::string_view name, std::optional<std::string> switch_val) {
				return unwrap(c.get(name, switch_val));
			},
			py::arg("name"), py::arg("switch_val") = py::none(),
			"Reads an int knob's value. switch_val is accepted for signature "
			"symmetry with eval() but ignored (Section 8: the int model has no "
			"per-group overrides in v1). Raises DetentError if missing/wrong-type.")
		.def(
			"health",
			[](const detent::Client& c) {
				auto h = c.health();
				py::dict d;
				d["initialized"] = h.initialized;
				d["source_connected"] = h.source_connected;
				d["accepted_version"] = h.accepted_version;
				d["staleness_seconds"] = h.staleness_seconds;
				d["bool_knobs"] = h.bool_knobs;
				d["int_knobs"] = h.int_knobs;
				d["cli_overrides"] = h.cli_overrides;
				return d;
			},
			"Section 12 diagnostic snapshot, as a dict.");
}
