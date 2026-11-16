#pragma once
#include "detent_relay/layout.hh"
#include <completion/api.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The relay's mirror (docs/design/namespace-delivery.md D7; docs/status/namespace-s8.md): per
// namespace, the upstream's base, blocks, level-0 sections and full copy, byte for byte, in one
// directory of its own. The mirror computes no layout and parses no file: it adopts the upstream's
// layout, fetches every file it lacks by name, verifies each digest, and publishes them under D6's
// commit (D8's `linkat` from `.tmp-` names, one D11 sync group, then the table by rename).
namespace detent::relay {
// Fetches one file of namespace `name_space` by its upstream name from the authority or an upstream
// relay, which authorize the read by namespace. Throws on failure.
using MirrorFetch = std::function<std::string(std::string_view name_space, const MirrorFile &)>;
struct AdoptStats {
	std::uint64_t fetched_files = 0, fetched_bytes = 0, reused_files = 0, collected_files = 0;
};
class FetchRefused : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};

// Qualification hook: runs after every primitive of startup, adoption, repair and collection, naming it.
// Install before opening a mirror, never concurrently with one.
struct MirrorTesting {
	static void install(void (*)(void *, const char *) noexcept, void *) noexcept;
};

// The mirror's one writer. It holds `owner.lock` exclusively for its lifetime; the directory holds
// nothing else of anyone's.
class MirrorStore {
  public:
	struct Options {
		unsigned mode = 0644;
		std::uint64_t maximum_table = std::uint64_t(1) << 30;
	};
	// Takes `owner.lock`, then D11's startup: one barrier, `.tmp-` names removed, the table read, each
	// reached file checked by name and size (never re-hashed: D12.2; digests are checked at first read),
	// and every name the table does not reach removed. A table that does not decode refuses the mirror;
	// a namespace with a file missing or of the wrong size is failed until it is adopted again.
	MirrorStore(std::shared_ptr<kronuz::io::completion::PosixIO>, Options);
	~MirrorStore();
	MirrorStore(const MirrorStore &) = delete;
	MirrorStore &operator=(const MirrorStore &) = delete;
	const MirrorTable &table() const noexcept;
	std::span<const std::string> failed() const noexcept;
	// Adopts each namespace's layout (replacing its entry) and drops each name in `dropped`. Files the
	// durable table reaches and holds are reused by name and never fetched; every other file is fetched,
	// checked against its size and digest, written under a `.tmp-` name and linked to its mirror name.
	// The fetched files and the directory sync as one group; then the table is written under a `.tmp-`
	// name, fsynced, renamed over `mirror` and the directory synced; then every file the new table no
	// longer reaches is unlinked. Nothing is published when a fetch fails or disagrees (FetchRefused),
	// and nothing is written when the durable table already holds the layout. `seal` is the upstream
	// seal of the answer adopted, never below the table's (std::invalid_argument).
	AdoptStats adopt(std::uint64_t seal, std::span<const NamespaceLayout>, std::span<const std::string> dropped,
					 const MirrorFetch &);
	// The bytes of a file the table reaches. Its digest is checked at its first read in this process;
	// a file that fails is fetched again by name, replaced durably, and its new bytes returned.
	std::string read(const MirrorFile &, const MirrorFetch &);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
