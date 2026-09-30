#pragma once

// ShmWriter: the daemon's only interaction with the shared-memory channel --
// serialize a detent::Snapshot into shm_format's flat layout and publish it
// under the seqlock protocol (see shm_format.hh). One writer, ever, for the
// lifetime of one detent-agent daemon: the caller (Agent) is responsible for
// only ever calling publish() from one thread at a time (the poller
// thread's InitOptions::on_update callback, plus one initial seed call right
// after construction -- never concurrently with each other).

#include "shm_format.hh"

#include "detent/model.hh"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace detent_agent {

class ShmWriter {
public:
	// Creates (or truncates) and mmaps `path`, sized for shm::kPayloadCapacity.
	// Throws std::runtime_error on any filesystem/mmap failure -- a daemon
	// that can't stand up its own publish channel should fail startup, not
	// limp along silently unable to serve any local reader.
	explicit ShmWriter(const std::filesystem::path& path);
	~ShmWriter();

	ShmWriter(const ShmWriter&) = delete;
	ShmWriter& operator=(const ShmWriter&) = delete;

	// Serializes `snapshot` and publishes it. Returns false (no publish
	// happened, the previous state is left intact) if the serialized form
	// would exceed shm::kPayloadCapacity -- a real but rare deployment
	// limit (see shm_format.hh), not a crash.
	bool publish(const detent::Snapshot& snapshot);

private:
	void* mapping_ = nullptr;
	std::size_t mapping_size_ = 0;
	int fd_ = -1;
};

// Exposed for the dedicated shm concurrency stress test (reader-vs-writer
// under TSan); not part of ShmWriter's own public surface since ordinary
// callers only ever need publish().
[[nodiscard]] std::vector<std::uint8_t> serialize_for_shm(const detent::Snapshot& snapshot);

} // namespace detent_agent
