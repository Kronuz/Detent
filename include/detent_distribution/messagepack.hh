#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace detent::distribution::messagepack {
enum class Kind { Array, Unsigned, Signed, String, Binary, Boolean, Nil };
struct Token {
	Kind kind = Kind::Nil;
	std::uint64_t number = 0;
	std::string bytes;
	std::int64_t signed_value() const {
		if (kind == Kind::Signed)
			return std::bit_cast<std::int64_t>(number);
		if (kind != Kind::Unsigned || number > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
			throw std::invalid_argument("MessagePack integer is outside signed range");
		return static_cast<std::int64_t>(number);
	}
};
struct Limits {
	std::uint64_t input_bytes = 256ull * 1024 * 1024;
	std::uint32_t string_bytes = 16384, binary_bytes = 32, array_elements = 500000;
};

// Incremental lexical decoder. It emits array lengths, not recursive objects;
// the schema owner validates tuple arity, nesting and aggregate record counts.
// A caller's byte budget bounds work even inside a fragmented string/header.
class Decoder {
  public:
	explicit Decoder(Limits limits = {}) : limits_(limits) {}
	std::optional<Token> next(std::span<const char> &input, std::size_t &budget) {
		if (failed_)
			throw std::logic_error("MessagePack decoder fenced");
		try {
			while (!input.empty() && budget) {
				if (total_ == limits_.input_bytes)
					throw std::length_error("MessagePack input limit exceeded");
				const auto byte = static_cast<unsigned char>(input.front());
				input = input.subspan(1);
				--budget;
				++total_;
				if (payload_) {
					token_.bytes.push_back(static_cast<char>(byte));
					if (--remaining_ == 0)
						return emit();
				} else if (needed_) {
					value_ = (value_ << 8) | byte;
					if (--needed_ == 0) {
						if (length_) {
							if (begin_payload())
								return emit();
						} else {
							if (token_.kind == Kind::Signed && width_ < 8 &&
								(value_ & (1ull << (width_ * 8 - 1))))
								value_ |= (~std::uint64_t{0}) << (width_ * 8);
							token_.number = value_;
							if (token_.kind == Kind::Array && value_ > limits_.array_elements)
								throw std::length_error("MessagePack array limit exceeded");
							return emit();
						}
					}
				} else {
					token_ = {};
					value_ = 0;
					if (byte <= 0x7f) {
						token_.kind = Kind::Unsigned;
						token_.number = byte;
						return emit();
					}
					if (byte >= 0xe0) {
						token_.kind = Kind::Signed;
						token_.number = std::bit_cast<std::uint64_t>(std::int64_t(byte) - 256);
						return emit();
					}
					if ((byte & 0xf0) == 0x90) {
						token_.kind = Kind::Array;
						token_.number = byte & 15;
						if (token_.number > limits_.array_elements)
							throw std::length_error("MessagePack array limit exceeded");
						return emit();
					}
					if ((byte & 0xe0) == 0xa0) {
						token_.kind = Kind::String;
						value_ = byte & 31;
						if (begin_payload())
							return emit();
						continue;
					}
					switch (byte) {
					case 0xc0:
						token_.kind = Kind::Nil;
						return emit();
					case 0xc2:
					case 0xc3:
						token_.kind = Kind::Boolean;
						token_.number = byte == 0xc3;
						return emit();
					case 0xcc:
					case 0xcd:
					case 0xce:
					case 0xcf:
						token_.kind = Kind::Unsigned;
						width_ = needed_ = 1u << (byte - 0xcc);
						break;
					case 0xd0:
					case 0xd1:
					case 0xd2:
					case 0xd3:
						token_.kind = Kind::Signed;
						width_ = needed_ = 1u << (byte - 0xd0);
						break;
					case 0xd9:
					case 0xda:
					case 0xdb:
						token_.kind = Kind::String;
						needed_ = 1u << (byte - 0xd9);
						length_ = true;
						break;
					case 0xc4:
					case 0xc5:
					case 0xc6:
						token_.kind = Kind::Binary;
						needed_ = 1u << (byte - 0xc4);
						length_ = true;
						break;
					case 0xdc:
					case 0xdd:
						token_.kind = Kind::Array;
						needed_ = width_ = 2u << (byte - 0xdc);
						break;
					default:
						throw std::invalid_argument("unsupported MessagePack type");
					}
				}
			}
			return std::nullopt;
		} catch (...) {
			failed_ = true;
			throw;
		}
	}
	void finish() {
		if (failed_ || payload_ || needed_) {
			failed_ = true;
			throw std::invalid_argument("truncated MessagePack token");
		}
	}
	std::uint64_t consumed() const noexcept { return total_; }

  private:
	bool begin_payload() {
		const auto maximum = token_.kind == Kind::String ? limits_.string_bytes : limits_.binary_bytes;
		if (value_ > maximum || value_ > limits_.input_bytes - total_)
			throw std::length_error("MessagePack payload limit exceeded");
		remaining_ = value_;
		length_ = false;
		payload_ = remaining_ != 0;
		return !payload_;
	}
	Token emit() {
		payload_ = false;
		length_ = false;
		needed_ = 0;
		value_ = 0;
		return std::move(token_);
	}
	Limits limits_;
	Token token_;
	std::uint64_t total_ = 0, value_ = 0, remaining_ = 0;
	unsigned needed_ = 0, width_ = 0;
	bool length_ = false, payload_ = false, failed_ = false;
};

inline void big_endian(std::string &out, std::uint64_t value, unsigned width) {
	for (unsigned i = width; i; --i)
		out.push_back(static_cast<char>(value >> ((i - 1) * 8)));
}
inline void unsigned_integer(std::string &out, std::uint64_t value) {
	if (value <= 127) {
		out.push_back(static_cast<char>(value));
		return;
	}
	unsigned width = value <= 0xff ? 1 : value <= 0xffff ? 2 : value <= 0xffffffffull ? 4 : 8;
	out.push_back(static_cast<char>(width == 1 ? 0xcc : width == 2 ? 0xcd : width == 4 ? 0xce : 0xcf));
	big_endian(out, value, width);
}
inline void signed_integer(std::string &out, std::int64_t value) {
	if (value >= 0) {
		unsigned_integer(out, static_cast<std::uint64_t>(value));
		return;
	}
	if (value >= -32) {
		out.push_back(static_cast<char>(value));
		return;
	}
	unsigned width = value >= -128										 ? 1
					 : value >= -32768									 ? 2
					 : value >= std::numeric_limits<std::int32_t>::min() ? 4
																		 : 8;
	out.push_back(static_cast<char>(width == 1 ? 0xd0 : width == 2 ? 0xd1 : width == 4 ? 0xd2 : 0xd3));
	big_endian(out, std::bit_cast<std::uint64_t>(value), width);
}
inline void array(std::string &out, std::uint32_t size) {
	if (size <= 15)
		out.push_back(static_cast<char>(0x90 | size));
	else {
		out.push_back(static_cast<char>(size <= 65535 ? 0xdc : 0xdd));
		big_endian(out, size, size <= 65535 ? 2 : 4);
	}
}
inline void string(std::string &out, std::string_view value, bool binary = false) {
	if (value.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::length_error("MessagePack string too large");
	if (!binary && value.size() <= 31)
		out.push_back(static_cast<char>(0xa0 | value.size()));
	else {
		unsigned width = value.size() <= 255 ? 1 : value.size() <= 65535 ? 2 : 4;
		out.push_back(static_cast<char>(binary ? (width == 1   ? 0xc4
												  : width == 2 ? 0xc5
															   : 0xc6)
											   : (width == 1   ? 0xd9
												  : width == 2 ? 0xda
															   : 0xdb)));
		big_endian(out, value.size(), width);
	}
	out.append(value);
}
} // namespace detent::distribution::messagepack
