#pragma once
#include "detent_distribution/session_frames.hh"
#include "detent_relay/mirror.hh"
#include <map>

// W8 (docs/design/namespace-delivery.md D7; docs/status/namespace-s8.md): a relay reads its
// upstream, the authority or another relay, and is never a member. It adopts the upstream's layout
// into its mirror, keeps the ring of entries after s from the upstream's W6 frames, and answers W5
// positions with W6 frames from its own disk and ring, so a downstream relay reads it as it reads
// the authority.
namespace detent::relay {
// A W5 position: a followed namespace and, when the reader holds a store of it, where that store is.
// `pages`: the reader takes this namespace's full copy, and a delta D6 picks, as a page transfer.
struct Position {
	std::string name;
	std::optional<distribution::NamespaceCursor> cursor;
	bool pages = false;
};
// What a relay reads from.
class Upstream {
  public:
	virtual ~Upstream() = default;
	// The layout of each named namespace the upstream holds, in name order, at one upstream seal
	// (the table's `seal`, sequence 0); a name it does not hold at that seal is absent.
	virtual MirrorTable layouts(std::span<const std::string> names) = 0;
	// One file of namespace `name_space`'s layout, by its upstream name.
	virtual std::string fetch(std::string_view name_space, const MirrorFile &) = 0;
	// The W6 body answering W5 positions, in strictly increasing name order.
	virtual std::string frames(std::span<const Position>) = 0;
	// The sources read in turn (the voters), and a move to the next one after an answer older than
	// the mirror's or a refusal to answer: a sync tries each source once before it fails.
	virtual std::size_t sources() const noexcept { return 1; }
	virtual void skip() {}
};
// The upstream answered below its own ring: it sealed between its layout and its frames.
class UpstreamMoved : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
// The upstream answered at a seal older than the mirror's, disagrees with it at the same seal
// (another voter that lags), or refused its layouts (503: a voter that knows no live leader):
// nothing is adopted or dropped, and the relay reads the next source.
class UpstreamBehind : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
struct SyncStats {
	AdoptStats files;
	std::uint64_t entries = 0; // ring positions received
};
// Dense distribution D6, D7 on the relay: full builds, and the answers sent as pages or kept as records.
struct PagesStats {
	std::uint64_t builds = 0, full_copies = 0, deltas = 0, delta_records = 0;
};

class NamespaceRelay final : public Upstream {
  public:
	// Opens the mirror (MirrorStore's startup). Every ring starts empty at its layout's s.
	NamespaceRelay(std::shared_ptr<kronuz::io::completion::PosixIO> mirror, distribution::Identity cluster,
				   Upstream &upstream, MirrorStore::Options = {});
	~NamespaceRelay();
	NamespaceRelay(const NamespaceRelay &) = delete;
	NamespaceRelay &operator=(const NamespaceRelay &) = delete;
	// Brings `names` (strictly increasing) to the upstream: adopts their layouts, fetching only
	// the files the mirror lacks and dropping a name the upstream no longer holds, then reads the
	// entries after each ring's head. Throws UpstreamMoved when the upstream sealed in between, and
	// UpstreamBehind when every source's answer is older than the mirror's.
	SyncStats sync(std::span<const std::string> names);
	const MirrorStore &mirror() const noexcept;
	// Where the relay holds a namespace: its incarnation and newest position.
	std::optional<distribution::NamespaceCursor> head(std::string_view name) const;

	MirrorTable layouts(std::span<const std::string> names) override;
	std::string fetch(std::string_view name_space, const MirrorFile &) override;
	std::string frames(std::span<const Position>) override;
	// The W6 body for a session's next, or nothing while every answer is Current or Ahead (the
	// session waits, as the authority's does). A position that takes pages gets a pages frame for a
	// full copy (dense distribution D7.4: the relay's build at its head, built once per position),
	// or for a delta from a base the page window holds when D6 finds the pages smaller.
	std::optional<std::string> changes(std::span<const Position>);
	const PagesStats &pages() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
