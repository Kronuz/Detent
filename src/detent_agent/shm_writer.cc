#include "shm_writer.hh"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace detent_agent {

namespace {

// Appends `data` to `blob`, returning its offset within `blob` (relative to
// blob's own start -- the caller adds the fixed record-array size to get
// the final payload-relative offset shm_format.hh's offsets require).
std::uint32_t append(std::vector<std::uint8_t>& blob, const void* data, std::size_t len) {
	auto offset = static_cast<std::uint32_t>(blob.size());
	auto* bytes = static_cast<const std::uint8_t*>(data);
	blob.insert(blob.end(), bytes, bytes + len);
	return offset;
}

// Pads `blob` with zero bytes so its size becomes a multiple of `alignment`
// (a power of two). Needed before writing any OverrideRecord[] span into the
// blob: unlike KnobRecord::name/OverrideRecord::name (raw char bytes, no
// alignment requirement), OverrideRecord itself has uint32_t members and the
// reader reinterpret_casts a pointer straight at payload+overrides_offset --
// an unaligned offset there is undefined behavior (UBSan caught this live:
// "reference binding to misaligned address ... requires 4 byte alignment").
void pad_to_alignment(std::vector<std::uint8_t>& blob, std::size_t alignment) {
	auto misalignment = blob.size() % alignment;
	if (misalignment != 0) {
		blob.resize(blob.size() + (alignment - misalignment), 0);
	}
}

} // namespace

std::vector<std::uint8_t> serialize_for_shm(const detent::Snapshot& snapshot) {
	// record_bytes is known immediately from the knob count alone, so every
	// offset below can be computed correctly on its first write -- record-
	// array-relative plus this constant -- with no later fix-up pass.
	const auto record_bytes = static_cast<std::uint32_t>(snapshot.knobs.size() * sizeof(shm::KnobRecord));

	std::vector<shm::KnobRecord> records;
	records.reserve(snapshot.knobs.size());
	std::vector<std::uint8_t> blob; // names + override arrays

	for (const auto& [name, def] : snapshot.knobs) {
		shm::KnobRecord rec{};
		rec.name_offset = record_bytes + append(blob, name.data(), name.size());
		rec.name_len = static_cast<std::uint32_t>(name.size());

		if (const auto* b = std::get_if<detent::BoolDef>(&def)) {
			rec.type = static_cast<std::uint8_t>(shm::KnobType::Bool);
			rec.default_value = b->default_value ? 1 : 0;
			rec.has_rollout_pct = b->rollout_pct.has_value() ? 1 : 0;
			rec.rollout_pct = b->rollout_pct.value_or(0);
			rec.override_count = static_cast<std::uint32_t>(b->overrides.size());
			if (rec.override_count > 0) {
				// Two passes, same reasoning as the outer KnobRecord array
				// vs. blob split: write all N OverrideRecord slots
				// *contiguously* first (the reader treats
				// payload+overrides_offset as a plain OverrideRecord[N]
				// array), then append every override's name bytes
				// afterward -- appending name bytes before each record, as
				// a single-pass version of this loop did originally,
				// interleaves [name0][OverrideRecord0][name1]... instead of
				// [OverrideRecord0][OverrideRecord1]...[name0][name1]...,
				// which silently broke every group-override lookup (the
				// reader's fixed-stride array indexing landed on the wrong
				// bytes entirely) -- confirmed live: a knob with a matching
				// switch_val override returned the *default*, not the
				// override, until this fix.
				//
				// The blob so far holds a run of raw, unpadded name bytes
				// (this knob's own name plus every prior knob's/override's
				// name), so its size is not generally a multiple of
				// alignof(OverrideRecord) -- pad up first, or the reader's
				// reinterpret_cast<const OverrideRecord*> lands on a
				// misaligned address (UBSan: "reference binding to
				// misaligned address ... requires 4 byte alignment").
				pad_to_alignment(blob, alignof(shm::OverrideRecord));
				rec.overrides_offset = record_bytes + static_cast<std::uint32_t>(blob.size());
				auto orecs_start = blob.size();
				blob.resize(blob.size() + rec.override_count * sizeof(shm::OverrideRecord));
				std::uint32_t idx = 0;
				for (const auto& [gkey, gval] : b->overrides) {
					shm::OverrideRecord orec{};
					orec.name_offset = record_bytes + append(blob, gkey.data(), gkey.size());
					orec.name_len = static_cast<std::uint32_t>(gkey.size());
					orec.value = gval ? 1 : 0;
					std::memcpy(blob.data() + orecs_start + idx * sizeof(shm::OverrideRecord), &orec, sizeof(orec));
					++idx;
				}
			}
		} else {
			const auto& i = std::get<detent::IntDef>(def);
			rec.type = static_cast<std::uint8_t>(shm::KnobType::Int);
			rec.default_value = i.default_value;
		}
		records.push_back(rec);
	}

	std::vector<std::uint8_t> payload;
	payload.reserve(record_bytes + blob.size());
	auto* rec_bytes = reinterpret_cast<const std::uint8_t*>(records.data());
	payload.insert(payload.end(), rec_bytes, rec_bytes + record_bytes);
	payload.insert(payload.end(), blob.begin(), blob.end());
	return payload;
}

ShmWriter::ShmWriter(const std::filesystem::path& path, unsigned mode) {
	mapping_size_ = sizeof(shm::Header) + shm::kPayloadCapacity;
	fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, mode);
	if (fd_ < 0) {
		throw std::runtime_error("detent-agent: shm open() failed: " + path.string());
	}
	if (::fchmod(fd_, mode) != 0) { ::close(fd_); throw std::runtime_error("cannot set snapshot permissions"); }
	if (::ftruncate(fd_, static_cast<off_t>(mapping_size_)) != 0) {
		::close(fd_);
		throw std::runtime_error("detent-agent: shm ftruncate() failed: " + path.string());
	}
	mapping_ = ::mmap(nullptr, mapping_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
	if (mapping_ == MAP_FAILED) {
		::close(fd_);
		throw std::runtime_error("detent-agent: shm mmap() failed: " + path.string());
	}
	// Placement-construct the header (in particular, its std::atomic<seq>)
	// into freshly-mapped memory rather than assuming zero-initialization
	// happens to be a valid atomic<uint64_t> state -- it is, on every
	// platform this project targets, but this is the correct, portable way
	// to establish an object's lifetime in raw mapped bytes regardless.
	new (mapping_) shm::Header();
}

ShmWriter::~ShmWriter() {
	if (mapping_ != nullptr && mapping_ != MAP_FAILED) {
		::munmap(mapping_, mapping_size_);
	}
	if (fd_ >= 0) {
		::close(fd_);
	}
}

bool ShmWriter::publish(const detent::Snapshot& snapshot) {
	auto payload = serialize_for_shm(snapshot);
	if (payload.size() > shm::kPayloadCapacity) {
		return false; // real deployment limit (shm_format.hh), not a crash
	}

	auto* header = static_cast<shm::Header*>(mapping_);
	auto* payload_area = static_cast<std::uint8_t*>(mapping_) + sizeof(shm::Header);

	// Seqlock write: odd while in flight, even once stable. Relaxed loads
	// are fine for a single-writer field we exclusively own; the fences
	// that matter are the release stores below, which is what gives a
	// reader's acquire-load-after-copy the guarantee it needs.
	auto seq = header->seq.load(std::memory_order_relaxed);
	header->seq.store(seq + 1, std::memory_order_release);
	std::memcpy(payload_area, payload.data(), payload.size());
	header->accepted_version = snapshot.version;
	header->published_at_unix_ms = 0; // Snapshot::published_at is a string timestamp, not tracked here (yet)
	header->knob_count = static_cast<std::uint32_t>(snapshot.knobs.size());
	header->payload_len = static_cast<std::uint32_t>(payload.size());
	header->seq.store(seq + 2, std::memory_order_release);
	return true;
}

} // namespace detent_agent
