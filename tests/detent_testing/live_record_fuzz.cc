// libFuzzer driver for the live record decoder (namespace design D10, step S3).
// Build: tests/detent_testing/build_live_fuzz.sh. Each input is decoded raw, then used to mutate a valid
// record whose digest is repaired, so mutations reach the structural checks behind the trailing SHA-256.
// Properties: no input crashes or leaves a sanitizer finding; a record that decodes encodes back to the
// same bytes.
#include "live_record.hh"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sha256.h>

namespace x = detent::local::detail;
namespace {
x::LiveRecord seed_record() {
	x::LiveRecord record;
	record.generation.fill('g');
	record.root_checksum.fill('r');
	record.read_digest.fill('d');
	for (std::size_t i = 0; i < record.histogram.size(); ++i)
		record.histogram[i] = std::uint32_t(i * 3);
	for (unsigned i = 0; i < 5; ++i) {
		x::LiveRow row;
		row.segment.fill(char(i + 1));
		row.length = 1000 + i;
		row.checksum.fill(char('a' + i));
		row.live = {i + 1, 100 + i};
		record.rows.push_back(row);
	}
	return record;
}
const std::vector<char> &seed() {
	static const std::vector<char> bytes = [] {
		const auto record = seed_record();
		std::vector<char> out(x::live_record_size(record.rows.size()));
		x::encode_live_record(record, out);
		return out;
	}();
	return bytes;
}
void probe(std::span<const char> bytes) {
	try {
		const auto decoded = x::decode_live_record(bytes, 1u << 12);
		std::vector<char> again(x::live_record_size(decoded.rows.size()));
		x::encode_live_record(decoded, again);
		if (!std::equal(again.begin(), again.end(), bytes.begin(), bytes.end()))
			std::abort();
	} catch (const std::invalid_argument &) {
	}
}
void reseal(std::vector<char> &bytes) {
	SHA256 hash;
	hash.add(bytes.data(), bytes.size() - x::live_digest_bytes);
	hash.getHash(reinterpret_cast<unsigned char *>(bytes.data() + bytes.size() - x::live_digest_bytes));
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
	probe({reinterpret_cast<const char *>(data), size});
	auto mutated = seed();
	for (std::size_t i = 0; i + 2 < size; i += 3) {
		const auto offset = ((std::size_t(data[i]) << 8) | data[i + 1]) % (mutated.size() - x::live_digest_bytes);
		mutated[offset] ^= char(data[i + 2]);
	}
	reseal(mutated);
	probe(mutated);
	mutated.resize(size % (mutated.size() + 1));
	probe(mutated);
	return 0;
}
