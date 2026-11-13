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
struct Position {
	std::string name;
	std::optional<distribution::NamespaceCursor> cursor;
};
// What a relay reads from.
class Upstream {
  public:
	virtual ~Upstream() = default;
	// The layout of each named namespace the upstream holds, in name order; a name it does not
	// hold is absent.
	virtual std::vector<NamespaceLayout> layouts(std::span<const std::string> names) = 0;
	// One file of a layout, by its upstream name.
	virtual std::string fetch(const MirrorFile &) = 0;
	// The W6 body answering W5 positions, in strictly increasing name order.
	virtual std::string frames(std::span<const Position>) = 0;
};
// The upstream answered below its own ring: it sealed between its layout and its frames.
class UpstreamMoved : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};
struct SyncStats {
	AdoptStats files;
	std::uint64_t entries = 0; // ring positions received
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
	// entries after each ring's head. Throws UpstreamMoved when the upstream sealed in between.
	SyncStats sync(std::span<const std::string> names);
	const MirrorStore &mirror() const noexcept;
	// Where the relay holds a namespace: its incarnation and newest position.
	std::optional<distribution::NamespaceCursor> head(std::string_view name) const;

	std::vector<NamespaceLayout> layouts(std::span<const std::string> names) override;
	std::string fetch(const MirrorFile &) override;
	std::string frames(std::span<const Position>) override;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::relay
