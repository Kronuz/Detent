#include "shm_reader.hh"

#include "detent/hashing.hh"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace detent_agent {

namespace {

// A writer in flight (seq odd) normally clears within microseconds -- the
// span between its two release-stores, bracketing a memcpy and a handful
// of field writes -- so the first several retries spin with no backoff at
// all, to not cost a normal reader anything measurable. Past that, the
// writer is either gone (crashed mid-publish, leaving seq odd forever) or
// badly stalled; yielding, then sleeping, bounds every reader's CPU use
// instead of spinning at 100% forever with no writer ever coming back to
// clear it.
class RetryBackoff {
public:
	void wait() {
		if (spins_ < 200) {
			++spins_;
			return; // pure spin: cheapest option for the common, brief case
		}
		if (spins_ < 2000) {
			++spins_;
			std::this_thread::yield();
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

private:
	int spins_ = 0;
};

// Every offset/length/count field below is read from a memory-mapped file
// this process doesn't own the writing of -- it can be torn mid-read by a
// racing publish (the seqlock's s1/s2 bracket only guarantees the *result*
// of a torn read gets discarded, not that the *act* of reading it can't
// itself walk out of bounds -- see field_name/find_knob/find_override
// below), or, more rarely, simply wrong in a corrupted/truncated file. So
// every dereference here is bounds-checked against `payload_len` (itself
// clamped to kPayloadCapacity, since it too comes straight from the
// mapping) before use; a field that doesn't fit is treated as "absent",
// never dereferenced, and the caller's seqlock retry is what decides
// whether the whole read must be redone (see the s1/s2 gate around every
// call site below) -- this function only has to be safe, not correct in
// isolation.

std::optional<std::string_view> field_name(std::uint32_t name_offset, std::uint32_t name_len, const std::uint8_t* payload, std::uint32_t payload_len) {
	// Widen to uint64_t before adding: name_offset/name_len are torn
	// uint32_t reads, so their sum can itself overflow uint32_t.
	std::uint64_t end = static_cast<std::uint64_t>(name_offset) + name_len;
	if (end > payload_len) return std::nullopt;
	return std::string_view(reinterpret_cast<const char*>(payload) + name_offset, name_len);
}

std::optional<std::string_view> field_name(const shm::KnobRecord& rec, const std::uint8_t* payload, std::uint32_t payload_len) {
	return field_name(rec.name_offset, rec.name_len, payload, payload_len);
}

std::optional<std::string_view> field_name(const shm::OverrideRecord& rec, const std::uint8_t* payload, std::uint32_t payload_len) {
	return field_name(rec.name_offset, rec.name_len, payload, payload_len);
}

const shm::KnobRecord* records_begin(const std::uint8_t* payload) {
	return reinterpret_cast<const shm::KnobRecord*>(payload);
}

// knob_count comes straight from the mapping too, so the array it claims to
// index can extend past payload_len -- clamp it to whatever actually fits
// before records_begin()+knob_count is ever formed, let alone dereferenced.
std::uint32_t bounded_knob_count(std::uint32_t knob_count, std::uint32_t payload_len) {
	auto max_records = payload_len / static_cast<std::uint32_t>(sizeof(shm::KnobRecord));
	return std::min(knob_count, max_records);
}

// Binary search: the writer serializes detent::Snapshot::knobs (a
// btree::map, always name-sorted) in iteration order, so the KnobRecord
// array is name-sorted for free -- this is a real binary search, not a
// linear scan dressed up as one. A record whose name doesn't fit
// (field_name() returns nullopt) sorts as empty rather than being skipped:
// std::lower_bound requires a strict weak ordering over the *whole* range,
// so silently omitting entries isn't an option here; a real caller's name
// is never empty, so this can only ever affect matching against garbage.
const shm::KnobRecord* find_knob(const std::uint8_t* payload, std::uint32_t payload_len, std::uint32_t knob_count, std::string_view name) {
	knob_count = bounded_knob_count(knob_count, payload_len);
	const shm::KnobRecord* begin = records_begin(payload);
	const shm::KnobRecord* end = begin + knob_count;
	auto it = std::lower_bound(begin, end, name, [payload, payload_len](const shm::KnobRecord& rec, std::string_view n) {
		return field_name(rec, payload, payload_len).value_or(std::string_view()) < n;
	});
	if (it != end && field_name(*it, payload, payload_len) == name) return &*it;
	return nullptr;
}

std::optional<bool> find_override(const shm::KnobRecord& rec, const std::uint8_t* payload, std::uint32_t payload_len, std::string_view switch_val) {
	std::uint64_t span_end = static_cast<std::uint64_t>(rec.overrides_offset) + static_cast<std::uint64_t>(rec.override_count) * sizeof(shm::OverrideRecord);
	if (rec.overrides_offset > payload_len || span_end > payload_len) return std::nullopt; // doesn't fit: treat as no overrides, not a crash
	const auto* overrides = reinterpret_cast<const shm::OverrideRecord*>(payload + rec.overrides_offset);
	for (std::uint32_t i = 0; i < rec.override_count; ++i) {
		if (field_name(overrides[i], payload, payload_len) == switch_val) return overrides[i].value != 0;
	}
	return std::nullopt;
}

} // namespace

std::optional<ShmReader> ShmReader::open(const std::filesystem::path& path) {
	if (!std::filesystem::exists(path)) return std::nullopt;

	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) return std::nullopt; // races with the daemon's own creation are treated the same as "not yet"

	auto mapping_size = sizeof(shm::Header) + shm::kPayloadCapacity;
	void* mapping = ::mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, fd, 0);
	::close(fd); // the mapping stays valid after close(); matches ShmWriter's own fd lifetime assumption
	if (mapping == MAP_FAILED) {
		throw std::runtime_error("detent-agent: shm mmap() failed: " + path.string());
	}

	const auto* header = static_cast<const shm::Header*>(mapping);
	if (header->magic != shm::kMagic || header->layout_version != shm::kLayoutVersion) {
		::munmap(mapping, mapping_size);
		throw std::runtime_error("detent-agent: shm file has an unrecognized/incompatible layout");
	}
	return ShmReader(mapping, mapping_size);
}

ShmReader::ShmReader(void* mapping, std::size_t mapping_size) : mapping_(mapping), mapping_size_(mapping_size) {}

ShmReader::ShmReader(ShmReader&& other) noexcept
	: mapping_(other.mapping_), mapping_size_(other.mapping_size_) {
	other.mapping_ = nullptr;
	other.mapping_size_ = 0;
}

ShmReader& ShmReader::operator=(ShmReader&& other) noexcept {
	if (this != &other) {
		if (mapping_ != nullptr) {
			::munmap(mapping_, mapping_size_);
		}
		mapping_ = other.mapping_;
		mapping_size_ = other.mapping_size_;
		other.mapping_ = nullptr;
		other.mapping_size_ = 0;
	}
	return *this;
}

ShmReader::~ShmReader() {
	if (mapping_ != nullptr) {
		::munmap(mapping_, mapping_size_);
	}
}

ShmStatus ShmReader::status() const {
	const auto* header = static_cast<const shm::Header*>(mapping_);
	RetryBackoff backoff;
	for (;;) {
		auto s1 = header->seq.load(std::memory_order_acquire);
		if (s1 & 1) { backoff.wait(); continue; } // writer in flight; retry
		ShmStatus st;
		st.accepted_version = header->accepted_version;
		st.knob_count = header->knob_count;
		auto s2 = header->seq.load(std::memory_order_acquire);
		if (s1 == s2) return st;
		backoff.wait();
	}
}

detent::Result<bool, detent::KnobError> ShmReader::eval(
	std::string_view name,
	std::optional<std::string_view> hash_val,
	std::optional<std::string_view> switch_val) const {
	const auto* header = static_cast<const shm::Header*>(mapping_);
	const auto* payload = static_cast<const std::uint8_t*>(mapping_) + sizeof(shm::Header);

	RetryBackoff backoff;
	for (;;) {
		auto s1 = header->seq.load(std::memory_order_acquire);
		if (s1 & 1) { backoff.wait(); continue; }

		auto knob_count = header->knob_count;
		auto payload_len = std::min(header->payload_len, static_cast<std::uint32_t>(shm::kPayloadCapacity));
		const auto* rec = find_knob(payload, payload_len, knob_count, name);
		if (rec == nullptr) {
			auto s2 = header->seq.load(std::memory_order_acquire);
			if (s1 != s2) { backoff.wait(); continue; } // the miss itself could have raced a write; retry before trusting it
			return detent::KnobError{detent::ErrorCategory::MissingKnob, std::string(name), "not present in current snapshot"};
		}
		if (rec->type != static_cast<std::uint8_t>(shm::KnobType::Bool)) {
			auto s2 = header->seq.load(std::memory_order_acquire);
			if (s1 != s2) { backoff.wait(); continue; }
			return detent::KnobError{detent::ErrorCategory::WrongType, std::string(name), "knob is int; eval() requires bool"};
		}

		// Section 8 precedence, identical to detent::Client::eval(): group
		// match, then rollout, then default.
		bool result;
		if (switch_val) {
			auto matched = find_override(*rec, payload, payload_len, *switch_val);
			if (matched) {
				result = *matched;
			} else if (rec->has_rollout_pct && hash_val) {
				result = detent::stable_bucket(name, *hash_val) < rec->rollout_pct;
			} else {
				result = rec->default_value != 0;
			}
		} else if (rec->has_rollout_pct && hash_val) {
			result = detent::stable_bucket(name, *hash_val) < rec->rollout_pct;
		} else {
			result = rec->default_value != 0;
		}

		auto s2 = header->seq.load(std::memory_order_acquire);
		if (s1 == s2) return result;
		// else: a write landed mid-evaluation; retry the whole lookup+eval.
		backoff.wait();
	}
}

detent::Result<std::int64_t, detent::KnobError> ShmReader::get(
	std::string_view name,
	std::optional<std::string_view> /*switch_val, reserved and ignored -- Section 8: int model has no group overrides*/) const {
	const auto* header = static_cast<const shm::Header*>(mapping_);
	const auto* payload = static_cast<const std::uint8_t*>(mapping_) + sizeof(shm::Header);

	RetryBackoff backoff;
	for (;;) {
		auto s1 = header->seq.load(std::memory_order_acquire);
		if (s1 & 1) { backoff.wait(); continue; }

		auto knob_count = header->knob_count;
		auto payload_len = std::min(header->payload_len, static_cast<std::uint32_t>(shm::kPayloadCapacity));
		const auto* rec = find_knob(payload, payload_len, knob_count, name);
		if (rec == nullptr) {
			auto s2 = header->seq.load(std::memory_order_acquire);
			if (s1 != s2) { backoff.wait(); continue; }
			return detent::KnobError{detent::ErrorCategory::MissingKnob, std::string(name), "not present in current snapshot"};
		}
		if (rec->type != static_cast<std::uint8_t>(shm::KnobType::Int)) {
			auto s2 = header->seq.load(std::memory_order_acquire);
			if (s1 != s2) { backoff.wait(); continue; }
			return detent::KnobError{detent::ErrorCategory::WrongType, std::string(name), "knob is bool; get() requires int"};
		}
		auto result = rec->default_value;

		auto s2 = header->seq.load(std::memory_order_acquire);
		if (s1 == s2) return result;
		backoff.wait();
	}
}

} // namespace detent_agent
