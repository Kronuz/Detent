#pragma once

// ShmReader: the CLI's only interaction with the shared-memory channel --
// mmap the file ShmWriter publishes and evaluate directly against the
// mapped bytes. No JSON parsing, no heap allocation on the read path, no
// daemon round-trip: the entire cost of a `detent get` is one mmap,
// a handful of atomic loads, a binary search over KnobRecord (the writer
// serializes detent::Snapshot's already-sorted btree::map in order, so this
// is a real binary search, not a scan), and (for a rollout knob)
// detent::stable_bucket() -- the exact same pure function detent::Client
// itself uses, reused unchanged.

#include "shm_format.hh"

#include "detent/errors.hh"
#include "detent/result.hh"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace detent_agent {

struct ShmStatus {
	std::uint64_t accepted_version = 0;
	std::uint32_t knob_count = 0;
};

class ShmReader {
public:
	// Returns std::nullopt (not an error) if `path` doesn't exist yet -- the
	// daemon has never published, which callers (main.cc) treat the same
	// way the old socket path treated "no daemon running": attempt an
	// auto-start, then retry. Throws std::runtime_error for a genuine
	// failure (exists but can't be opened/mapped, or fails the magic/
	// layout_version sanity check).
	[[nodiscard]] static std::optional<ShmReader> open(const std::filesystem::path& path);

	~ShmReader();
	ShmReader(ShmReader&& other) noexcept;
	ShmReader& operator=(ShmReader&& other) noexcept;
	ShmReader(const ShmReader&) = delete;
	ShmReader& operator=(const ShmReader&) = delete;

	[[nodiscard]] ShmStatus status() const;

	[[nodiscard]] detent::Result<bool, detent::KnobError> eval(
		std::string_view name,
		std::optional<std::string_view> hash_val,
		std::optional<std::string_view> switch_val) const;

	[[nodiscard]] detent::Result<std::int64_t, detent::KnobError> get(
		std::string_view name,
		std::optional<std::string_view> switch_val) const;

private:
	explicit ShmReader(void* mapping, std::size_t mapping_size);

	void* mapping_ = nullptr;
	std::size_t mapping_size_ = 0;
};

} // namespace detent_agent
