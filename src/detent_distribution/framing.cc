#include "detent_distribution/framing.hh"
#include <algorithm>
#include <array>
#include <cstring>
#include <sha256.h>
#include <zstd.h>

namespace detent::distribution {
namespace {
constexpr std::string_view magic = "DTDIST01";
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
void put(std::string &out, std::uint64_t value, unsigned bytes) {
	for (unsigned i = bytes; i; --i)
		out.push_back(static_cast<char>(value >> ((i - 1) * 8)));
}
std::uint64_t get(std::span<const char> header, std::size_t offset, unsigned bytes) {
	std::uint64_t value = 0;
	for (unsigned i = 0; i < bytes; ++i)
		value = (value << 8) | static_cast<unsigned char>(header[offset + i]);
	return value;
}
void check_fragment(const Fragment &fragment) {
	require(fragment.kind == UpdateKind::Snapshot || fragment.kind == UpdateKind::Delta ||
				fragment.kind == UpdateKind::Progress,
			"unknown distribution frame kind");
	require(fragment.update != Identity{} && fragment.bytes.size() <= fragment_bytes &&
				fragment.offset <= fragment.total &&
				fragment.bytes.size() <= fragment.total - fragment.offset,
			"invalid distribution fragment bounds");
	require(fragment.final == (fragment.bytes.size() == fragment.total - fragment.offset) &&
				(fragment.final || !fragment.bytes.empty()),
			"distribution fragment final/offset mismatch");
}
} // namespace
std::string encode_fragment(const Fragment &fragment, bool compress) {
	check_fragment(fragment);
	std::string payload;
	if (compress) {
		payload.resize(ZSTD_compressBound(fragment.bytes.size()));
		auto count =
			ZSTD_compress(payload.data(), payload.size(), fragment.bytes.data(), fragment.bytes.size(), 3);
		if (ZSTD_isError(count))
			throw std::runtime_error("distribution fragment compression failed");
		payload.resize(count);
	} else
		payload = fragment.bytes;
	std::string header(magic);
	header.push_back(1);
	header.push_back(static_cast<char>(fragment.kind));
	put(header, (compress ? 1 : 0) | (fragment.final ? 2 : 0), 2);
	put(header, payload.size(), 4);
	put(header, fragment.bytes.size(), 4);
	put(header, 0, 4);
	header.append(fragment.update.data(), fragment.update.size());
	put(header, fragment.offset, 8);
	put(header, fragment.total, 8);
	put(header, 0, 8);
	SHA256 hash;
	hash.add(header.data(), header.size());
	hash.add(payload.data(), payload.size());
	unsigned char digest[SHA256::HashBytes];
	hash.getHash(digest);
	header.append(reinterpret_cast<const char *>(digest), sizeof(digest));
	header.append(payload);
	return header;
}

struct FragmentDecoder::Impl {
	UpdateKind kind;
	std::uint64_t maximum, next_offset = 0, total = 0;
	bool compression, admitted = false, final = false, failed = false;
	Identity update{};
	std::array<char, frame_header_bytes> header{};
	std::size_t header_size = 0, encoded_size = 0, decoded_size = 0;
	bool compressed = false;
	Fragment current;
	std::string payload;
	SHA256 hash;
	std::optional<Fragment> output;
	Impl(UpdateKind expected, std::uint64_t maximum_bytes, bool allow)
		: kind(expected), maximum(maximum_bytes), compression(allow) {
		require(kind == UpdateKind::Snapshot || kind == UpdateKind::Delta || kind == UpdateKind::Progress,
				"unknown expected distribution kind");
	}
	void parse_header() {
		require(std::string_view(header.data(), magic.size()) == magic && header[8] == 1,
				"unsupported distribution framing");
		const auto flags = get(header, 10, 2);
		require((flags & ~3ull) == 0 && get(header, 20, 4) == 0 && get(header, 56, 8) == 0,
				"unknown distribution frame flags/reserved fields");
		compressed = flags & 1;
		require(!compressed || compression, "unnegotiated fragment compression");
		current = {};
		current.kind = static_cast<UpdateKind>(static_cast<unsigned char>(header[9]));
		current.final = flags & 2;
		std::copy_n(header.data() + 24, 16, current.update.begin());
		current.offset = get(header, 40, 8);
		current.total = get(header, 48, 8);
		encoded_size = get(header, 12, 4);
		decoded_size = get(header, 16, 4);
		require(current.kind == kind && current.update != Identity{} && current.total <= maximum &&
					current.offset <= current.total && decoded_size <= current.total - current.offset &&
					decoded_size <= fragment_bytes,
				"distribution frame identity/bounds mismatch");
		require(encoded_size <= ZSTD_compressBound(fragment_bytes) &&
					(compressed || encoded_size == decoded_size),
				"distribution encoded fragment bound exceeded");
		require(current.final == (decoded_size == current.total - current.offset) &&
					(current.final || decoded_size),
				"distribution final fragment mismatch");
		require(current.offset == next_offset, "nonsequential distribution fragment");
		if (admitted)
			require(current.update == update && current.total == total,
					"distribution update changed between fragments");
		else {
			update = current.update;
			total = current.total;
			admitted = true;
		}
		payload.clear();
		payload.reserve(encoded_size);
		hash.reset();
		hash.add(header.data(), 64);
	}
	void complete() {
		unsigned char digest[SHA256::HashBytes];
		hash.getHash(digest);
		require(std::memcmp(digest, header.data() + 64, sizeof(digest)) == 0,
				"distribution fragment checksum mismatch");
		if (compressed) {
			current.bytes.resize(decoded_size);
			auto count =
				ZSTD_decompress(current.bytes.data(), current.bytes.size(), payload.data(), payload.size());
			require(!ZSTD_isError(count) && count == decoded_size,
					"distribution fragment decompression mismatch");
		} else
			current.bytes = std::move(payload);
		next_offset += decoded_size;
		final = current.final;
		output = std::move(current);
		header_size = 0;
	}
};
FragmentDecoder::FragmentDecoder(UpdateKind expected_kind, std::uint64_t maximum_bytes,
								 bool allow_compression)
	: impl_(std::make_unique<Impl>(expected_kind, maximum_bytes, allow_compression)) {}
FragmentDecoder::~FragmentDecoder() = default;
FragmentDecoder::FragmentDecoder(FragmentDecoder &&) noexcept = default;
void FragmentDecoder::feed(std::span<const char> &input, WorkBudget &budget) {
	require(impl_ && !impl_->failed, "distribution fragment decoder unavailable");
	try {
		while (!input.empty() && budget.bytes && budget.tokens && !impl_->output) {
			require(!impl_->final, "trailing distribution frame bytes");
			if (impl_->header_size < frame_header_bytes) {
				auto count = std::min({input.size(), budget.bytes, frame_header_bytes - impl_->header_size});
				std::copy_n(input.data(), count, impl_->header.data() + impl_->header_size);
				input = input.subspan(count);
				budget.bytes -= count;
				impl_->header_size += count;
				if (impl_->header_size == frame_header_bytes) {
					--budget.tokens;
					impl_->parse_header();
					if (!impl_->encoded_size)
						impl_->complete();
				}
			} else {
				auto count =
					std::min({input.size(), budget.bytes, impl_->encoded_size - impl_->payload.size()});
				impl_->payload.append(input.data(), count);
				impl_->hash.add(input.data(), count);
				input = input.subspan(count);
				budget.bytes -= count;
				if (impl_->payload.size() == impl_->encoded_size) {
					--budget.tokens;
					impl_->complete();
				}
			}
		}
	} catch (...) {
		impl_->failed = true;
		throw;
	}
}
std::optional<Fragment> FragmentDecoder::take() {
	require(impl_ && !impl_->failed, "distribution fragment decoder unavailable");
	auto result = std::move(impl_->output);
	impl_->output.reset();
	return result;
}
void FragmentDecoder::finish() {
	require(impl_ && !impl_->failed, "distribution fragment decoder unavailable");
	if (!impl_->final || impl_->output || impl_->header_size) {
		impl_->failed = true;
		throw std::invalid_argument("incomplete distribution framing");
	}
}
} // namespace detent::distribution
