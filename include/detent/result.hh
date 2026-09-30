#pragma once

// A tiny expected<T, E>-shaped result type. The project targets C++20, and
// std::expected is C++23-only, so this fills that one gap rather than
// pulling in a third-party expected implementation for a single small type.

#include <cassert>
#include <optional>
#include <utility>
#include <variant>

namespace detent {

template <typename T, typename E>
class Result {
public:
	Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
	Result(E error) : storage_(std::in_place_index<1>, std::move(error)) {}

	[[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
	explicit operator bool() const noexcept { return has_value(); }

	[[nodiscard]] const T& value() const& {
		assert(has_value());
		return std::get<0>(storage_);
	}
	[[nodiscard]] T& value() & {
		assert(has_value());
		return std::get<0>(storage_);
	}
	[[nodiscard]] T&& value() && {
		assert(has_value());
		return std::get<0>(std::move(storage_));
	}

	[[nodiscard]] const E& error() const& {
		assert(!has_value());
		return std::get<1>(storage_);
	}
	[[nodiscard]] E&& error() && {
		assert(!has_value());
		return std::get<1>(std::move(storage_));
	}

private:
	std::variant<T, E> storage_;
};

// void-value specialization, for operations that either succeed or produce
// an error (e.g. "was this update accepted").
template <typename E>
class Result<void, E> {
public:
	Result() : error_() {}
	Result(E error) : error_(std::move(error)), has_error_(true) {}

	[[nodiscard]] bool has_value() const noexcept { return !has_error_; }
	explicit operator bool() const noexcept { return has_value(); }

	[[nodiscard]] const E& error() const& {
		assert(!has_value());
		return *error_;
	}

private:
	std::optional<E> error_;
	bool has_error_ = false;
};

} // namespace detent
