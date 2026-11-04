#pragma once
// Header-only C++ face of libdetent_reader (docs/design/reader.md D7; docs/mechanisms/reader-core.md).
// Every lookup returns the caller's default on any error; lookup() also gives the status (Q2 (a)).
#include "detent/reader.h"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace detent {
class ReaderError : public std::runtime_error {
  public:
	ReaderError(detent_status status, const std::string &what) : std::runtime_error(what), status_(status) {}
	detent_status status() const noexcept { return status_; }

  private:
	detent_status status_;
};
struct Lookup {
	detent_status status;
	detent_value value;
	explicit operator bool() const noexcept { return status == DETENT_OK; }
};
// One handle per process, owned by the application (R4). Lookups are thread-safe; moving or
// destroying the reader must not race them.
class Reader {
  public:
	// Throws ReaderError when the library's ABI differs from this header's or the open fails.
	explicit Reader(std::string_view store, std::uint32_t publisher_uid,
	                const std::vector<std::string_view> &namespaces = {}) {
		if (detent_abi_version() != DETENT_ABI_VERSION)
			throw ReaderError(DETENT_INVALID_ARGUMENT, "libdetent_reader ABI version differs from its header");
		std::vector<detent_str> names;
		names.reserve(namespaces.size());
		for (const auto ns : namespaces)
			names.push_back(str(ns));
		detent_open_options options{sizeof(detent_open_options), str(store), names.data(), names.size(),
		                            publisher_uid};
		const auto status = detent_open(&options, &handle_);
		if (status != DETENT_OK)
			throw ReaderError(status, std::string("detent_open: ") + detent_status_name(status));
	}
	Reader(Reader &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
	Reader &operator=(Reader &&other) noexcept {
		if (this != &other) {
			detent_close(handle_);
			handle_ = std::exchange(other.handle_, nullptr);
		}
		return *this;
	}
	Reader(const Reader &) = delete;
	Reader &operator=(const Reader &) = delete;
	~Reader() { detent_close(handle_); }

	bool eval(std::string_view name, bool fallback, std::optional<std::string_view> hash = {},
	          std::optional<std::string_view> switch_value = {}) const noexcept {
		bool out = fallback;
		detent_eval(handle_, str(name), str(hash), str(switch_value), fallback, &out);
		return out;
	}
	std::int64_t get(std::string_view name, std::int64_t fallback) const noexcept {
		std::int64_t out = fallback;
		detent_get(handle_, str(name), fallback, &out);
		return out;
	}
	Lookup lookup(std::string_view name, std::optional<std::string_view> hash = {},
	              std::optional<std::string_view> switch_value = {}) const noexcept {
		Lookup out{DETENT_OK, {}};
		out.status = detent_read(handle_, str(name), str(hash), str(switch_value), &out.value);
		return out;
	}
	detent_status refresh() const noexcept { return detent_refresh(handle_); }
	std::optional<std::uint64_t> revision(std::string_view name_space) const noexcept {
		std::uint64_t out = 0;
		if (detent_revision(handle_, str(name_space), &out) != DETENT_OK)
			return std::nullopt;
		return out;
	}
	detent_reader *native() const noexcept { return handle_; }

  private:
	static detent_str str(std::string_view s) noexcept { return {s.data() ? s.data() : "", s.size()}; }
	static detent_str str(std::optional<std::string_view> s) noexcept {
		return s ? str(*s) : detent_str{nullptr, 0};
	}
	detent_reader *handle_ = nullptr;
};
} // namespace detent
