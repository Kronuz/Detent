#pragma once

// The on-disk/in-shared-memory binary layout detent-agent uses to publish a
// pre-parsed, ready-to-evaluate snapshot from the daemon to every local
// reader (the `detent-agent get` CLI, a fresh process each invocation) --
// replacing the previous Unix-socket request/response protocol entirely.
// Private to detent_agent; not a public wire format anyone else consumes.
//
// Design:
//   - The daemon (the only writer) already runs a real detent::Client to
//     poll/parse/validate/merge (Section 4/7/10's machinery, reused
//     unchanged via InitOptions::on_update, not reimplemented here). On
//     every accepted update it serializes the resulting Snapshot into this
//     flat, pointer-free, fixed-capacity binary layout and publishes it
//     with a seqlock (below) -- a single fixed-size region, not double-
//     buffered: with updates far rarer than reads, a plain seqlock over
//     one buffer is both simpler to reason about and avoids the "reader
//     still mid-copy from a buffer the writer wants to reuse" edge case a
//     naive two-buffer design would have to solve separately.
//   - The reader (`detent-agent get`, a fresh process each call) mmaps the
//     file PROT_READ and evaluates directly against the mapped bytes: no
//     JSON parsing, no heap allocation, no daemon round-trip at all for a
//     read -- string_views point straight into the mapping, and
//     detent::stable_bucket() (already a pure function over string_views)
//     is reused unchanged for the rollout hash.
//
// Seqlock protocol (classic, e.g. Linux's own timekeeping):
//   writer: seq++ (now odd) -> memcpy new payload + update header fields
//           -> seq++ (now even)
//   reader: loop { s1 = seq.load(acquire); if (s1 & 1) retry;
//                  <read fields / do work>;
//                  s2 = seq.load(acquire); if (s1 != s2) retry; }
// Wait-free for the writer (never blocks on a reader); a reader only
// retries if it genuinely raced a write, which -- given updates happen on
// a poll_interval measured in hundreds of milliseconds, not microseconds
// -- is rare in practice.
//
// Two things worth being honest about, from a dedicated review of this
// exact design:
//   - Bounds safety: every offset/length/count the reader dereferences
//     (name_offset, name_len, overrides_offset, override_count, knob_count)
//     is read from this mapped file, so a read can observe TORN bytes mid-
//     write, not just a torn *result* -- the s1/s2 bracket above only
//     guarantees a bad *result* gets discarded and retried, not that the
//     act of reading a torn offset can't itself walk out of the mapping.
//     shm_reader.cc therefore bounds-checks every such field against the
//     current payload_len (itself clamped to kPayloadCapacity) before
//     dereferencing it, treating anything that doesn't fit as "absent" and
//     letting the ordinary seqlock retry resolve whether that was a
//     genuine race or real corruption.
//   - Formal data-race status: the writer's plain (non-atomic) memcpy of
//     the payload, and its plain stores to accepted_version/knob_count/
//     payload_len, race with readers' plain loads of the same bytes under
//     the ISO C++ object model -- this is the same "seqlock is technically
//     UB" gap the Linux kernel's own seqlock has always had in C. It is
//     correct on every architecture this project targets (the acquire/
//     release fences on `seq` are what make the *result* correct; the
//     underlying bytes being read mid-write is relied on to be
//     hardware-benign, not formally sanctioned by the standard) and is a
//     deliberate, well-understood tradeoff, not an oversight -- but it is
//     not something a strict TSan run can be expected to certify as clean
//     in the general case, only in whatever schedules it happens to
//     exercise.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace detent_agent::shm {

inline constexpr std::uint32_t kMagic = 0x444e5431; // "1TND" little-endian; a sanity marker, not a format negotiation
inline constexpr std::uint32_t kLayoutVersion = 1;
// 4 MiB of payload comfortably covers realistic knob counts (the project's
// own default max_knobs is 100,000, but a single detent-agent deployment
// serving a handful of services is realistically dozens to low thousands);
// a snapshot that doesn't fit is simply not published (ShmWriter::publish
// returns false; the daemon logs and keeps serving the last good publish)
// rather than corrupting anything.
inline constexpr std::size_t kPayloadCapacity = 4u * 1024 * 1024;

// Fixed-size, trivially-copyable header at the start of the mapped file.
// std::atomic<std::uint64_t> is required to be lock-free on every platform
// this project targets (static_assert below) precisely because ordinary
// language-level atomics -- not a named OS mutex/semaphore -- are what make
// this safe to share between processes that don't otherwise synchronize.
struct Header {
	std::uint32_t magic = kMagic;
	std::uint32_t layout_version = kLayoutVersion;
	std::atomic<std::uint64_t> seq{0}; // even = stable, odd = write in progress
	std::uint64_t accepted_version = 0;
	std::uint64_t published_at_unix_ms = 0; // 0 if the snapshot carried no published_at
	std::uint32_t knob_count = 0;
	std::uint32_t payload_len = 0; // bytes actually used in the payload area below
};
static_assert(std::is_standard_layout_v<Header>);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

// One knob's fixed-size fields. Variable-length data (the name, and bool
// knobs' override entries) lives in the payload's blob region, referenced
// by offset+length relative to the start of the payload area (i.e. the
// first byte after this file's KnobRecord array) -- never an absolute
// pointer, which would be meaningless across address spaces.
struct KnobRecord {
	std::uint32_t name_offset = 0;
	std::uint32_t name_len = 0;
	std::uint8_t type = 0; // 0 = bool, 1 = int
	std::uint8_t has_rollout_pct = 0;
	std::uint8_t rollout_pct = 0; // 0..100, meaningful only if has_rollout_pct
	std::uint8_t reserved = 0;
	std::int64_t default_value = 0; // bool: 0/1; int: the full value
	std::uint32_t override_count = 0;
	std::uint32_t overrides_offset = 0; // override_count * OverrideRecord follow, in the blob region
};
static_assert(std::is_trivially_copyable_v<KnobRecord>);
static_assert(std::is_standard_layout_v<KnobRecord>);
static_assert(sizeof(KnobRecord) == 32);

// One per-group bool override. Int knobs never have overrides (Section 8:
// "the specified integer model has no group overrides"), so this type is
// bool-only by construction, not by a runtime type tag.
struct OverrideRecord {
	std::uint32_t name_offset = 0;
	std::uint32_t name_len = 0;
	std::uint8_t value = 0; // 0/1
};
static_assert(std::is_trivially_copyable_v<OverrideRecord>);
static_assert(std::is_standard_layout_v<OverrideRecord>);

enum class KnobType : std::uint8_t { Bool = 0, Int = 1 };

} // namespace detent_agent::shm
