#pragma once
#include <cstdint>

namespace detent::local {
// What a store asked storage to make durable, counted from the completed sync requests.
// `device_flushes` follows completion's platform contract (docs/mechanisms/sync-files.md): on macOS
// one F_FULLFSYNC per Sync, per DirectorySync and per SyncFiles group; on Linux and FreeBSD one
// fsync per file and per directory, each a device flush on ext4 and XFS.
struct SyncCounters {
	std::uint64_t files = 0;            // files made durable: Sync requests and SyncFiles members
	std::uint64_t directory_syncs = 0;  // DirectorySync requests and the directory step of each group
	std::uint64_t device_flushes = 0;
	std::uint64_t groups = 0;           // SyncFiles and OrderFiles requests
	friend bool operator==(const SyncCounters &, const SyncCounters &) = default;
	friend SyncCounters operator-(const SyncCounters &a, const SyncCounters &b) noexcept {
		return {a.files - b.files, a.directory_syncs - b.directory_syncs, a.device_flushes - b.device_flushes,
				a.groups - b.groups};
	}
};
// Since the store opened (`total`), until its startup finished (`startup`: the census barrier, or
// the creation of a fresh store), and the last completed publication's own (`publication`).
struct SyncStats {
	SyncCounters total, startup, publication;
};
} // namespace detent::local
