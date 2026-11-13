#include "detent_relay/namespace_relay.hh"
#include "history_format.hh"
#include <algorithm>

namespace detent::relay {
namespace d = distribution;
namespace h = authority::history;
namespace {
constexpr int sync_attempts = 3;

struct Entry {
	std::string key;
	std::optional<KnobDef> value;
};
// Entries after s, mirrored from the upstream's frames: by position, each in key order.
struct Ring {
	d::Identity incarnation{};
	std::uint64_t head = 0;
	std::map<std::uint64_t, std::vector<Entry>> entries;
};

// A decoded block, base or copy's records, each definition decoded as it is reached.
class ChangesReader final : public d::HistoryReader {
  public:
	explicit ChangesReader(h::Changes changes) : changes_(std::move(changes)) {}
	const d::HistoryRecord *next() override {
		if (at_ == changes_.size())
			return nullptr;
		const auto &change = changes_[at_++];
		if (change.definition)
			value_ = authority::decode_definition(*change.definition);
		record_ = {change.key, change.revision, change.definition ? &value_ : nullptr};
		return &record_;
	}

  private:
	h::Changes changes_;
	std::size_t at_ = 0;
	KnobDef value_;
	d::HistoryRecord record_;
};
// Reads a mirrored file's changes for an answer.
class Tiers {
  public:
	virtual h::Changes changes(const NamespaceLayout &, const MirrorFile &) = 0;

  protected:
	~Tiers() = default;
};
class RingReader final : public d::HistoryReader {
  public:
	RingReader(const Ring &ring, std::uint64_t after) : at_(ring.entries.upper_bound(after)), end_(ring.entries.end()) {}
	const d::HistoryRecord *next() override {
		while (at_ != end_ && index_ == at_->second.size()) {
			++at_;
			index_ = 0;
		}
		if (at_ == end_)
			return nullptr;
		const auto &entry = at_->second[index_++];
		record_ = {entry.key, at_->first, entry.value ? &*entry.value : nullptr};
		return &record_;
	}

  private:
	std::map<std::uint64_t, std::vector<Entry>>::const_iterator at_, end_;
	std::size_t index_ = 0;
	d::HistoryRecord record_;
};
} // namespace

struct NamespaceRelay::Impl final : Tiers {
	MirrorStore store;
	d::Identity cluster;
	Upstream &upstream;
	std::map<std::string, Ring, std::less<>> rings;

	Impl(std::shared_ptr<kronuz::io::completion::PosixIO> io, d::Identity cluster, Upstream &upstream,
		 MirrorStore::Options options)
		: store(std::move(io), options), cluster(cluster), upstream(upstream) {
		for (const auto &layout : store.table().entries)
			rings[layout.name] = {layout.incarnation, layout.end(), {}};
	}
	MirrorFetch fetcher() {
		return [this](const MirrorFile &file) { return upstream.fetch(file); };
	}
	bool serves(const NamespaceLayout &layout) const {
		const auto failed = store.failed();
		return std::find(failed.begin(), failed.end(), layout.name) == failed.end();
	}
	// The changes of one mirrored file, checked against the layout that names it.
	h::Changes changes(const NamespaceLayout &layout, const MirrorFile &file) override {
		const auto bytes = store.read(file, fetcher());
		auto block = file.kind == MirrorFile::Kind::Block || file.kind == MirrorFile::Kind::Section
						 ? h::decode_block(bytes, {})
						 : h::decode_base(bytes, {});
		if (block.name_space != layout.name || block.incarnation != layout.incarnation || block.first != file.first ||
			block.last != file.last)
			throw d::HistoryFailure("relay mirror: a file holds another range: " + file.name);
		return std::move(block.changes);
	}
	SyncStats attempt(std::span<const std::string> names);
};

namespace {
// One namespace's history, frozen for one answer: the mirror's tiers and the relay's ring.
class MirrorHistory final : public d::NamespaceHistory {
  public:
	MirrorHistory(Tiers &tiers, const d::Identity &cluster, const NamespaceLayout &layout, const Ring &ring)
		: relay_(tiers), cluster_(cluster), layout_(layout), ring_(ring) {
		for (const auto &file : layout.files)
			if (file.kind == MirrorFile::Kind::Block || file.kind == MirrorFile::Kind::Section) {
				tiers_.push_back(&file);
				spans_.push_back({file.first, file.last, file.kind == MirrorFile::Kind::Block});
			}
	}
	d::NamespaceCursor head() const noexcept override { return {cluster_, layout_.incarnation, ring_.head}; }
	std::uint64_t cut() const noexcept override { return layout_.cut; }
	std::uint64_t ring_start() const noexcept override { return layout_.end(); }
	std::span<const d::HistorySpan> spans() const noexcept override { return spans_; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t index) const override {
		return std::make_unique<ChangesReader>(relay_.changes(layout_, *tiers_.at(index)));
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
		return std::make_unique<RingReader>(ring_, after);
	}
	// The cached copy at s, or the base and every tier merged without tombstones.
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		if (!layout_.files.empty() && layout_.files.back().kind == MirrorFile::Kind::Copy)
			return std::make_unique<ChangesReader>(relay_.changes(layout_, layout_.files.back()));
		h::Changes copy;
		for (const auto &file : layout_.files)
			copy = h::merge(copy, relay_.changes(layout_, file), true);
		return std::make_unique<ChangesReader>(std::move(copy));
	}

  private:
	Tiers &relay_;
	d::Identity cluster_;
	const NamespaceLayout &layout_;
	const Ring &ring_;
	std::vector<const MirrorFile *> tiers_;
	std::vector<d::HistorySpan> spans_;
};

// Stages the entries of the upstream's frames; any other answer means the upstream moved.
class RingSink final : public d::FrameSink {
  public:
	RingSink(const d::Identity &cluster, const std::map<std::string, Ring, std::less<>> &rings)
		: cluster_(cluster), rings_(rings) {}
	std::map<std::string, std::map<std::uint64_t, std::vector<Entry>>, std::less<>> staged;
	std::map<std::string, std::uint64_t, std::less<>> heads;
	void frame(std::string_view name, d::FrameKind kind, std::uint64_t after, const d::NamespaceCursor &to) override {
		const auto ring = rings_.find(name);
		if (ring == rings_.end())
			throw std::runtime_error("relay: the upstream answered a namespace it was not asked for");
		if (kind != d::FrameKind::Entries || after != ring->second.head || to.cluster != cluster_ ||
			to.incarnation != ring->second.incarnation)
			throw UpstreamMoved("relay: the upstream answered below its ring");
		name_ = name;
		heads[name_] = to.position;
		entries_ = &staged[name_];
	}
	void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override {
		if (kind != d::PieceKind::Entry || first != last)
			throw UpstreamMoved("relay: the upstream answered below its ring");
		position_ = first;
		(*entries_)[position_];
	}
	void record(std::string_view key, std::uint64_t, const KnobDef *value) override {
		(*entries_)[position_].push_back({std::string(key), value ? std::optional<KnobDef>(*value) : std::nullopt});
	}

  private:
	d::Identity cluster_;
	const std::map<std::string, Ring, std::less<>> &rings_;
	std::string name_;
	std::map<std::uint64_t, std::vector<Entry>> *entries_ = nullptr;
	std::uint64_t position_ = 0;
};
} // namespace

SyncStats NamespaceRelay::Impl::attempt(std::span<const std::string> names) {
	const auto layouts = upstream.layouts(names);
	std::vector<std::string> dropped;
	for (const auto &name : names)
		if (std::none_of(layouts.begin(), layouts.end(), [&](const auto &layout) { return layout.name == name; }))
			dropped.push_back(name);
	for (const auto &layout : layouts)
		if (!std::binary_search(names.begin(), names.end(), layout.name))
			throw std::runtime_error("relay: the upstream sent a namespace it was not asked for");
	SyncStats stats;
	stats.files = store.adopt(layouts, dropped, fetcher());
	for (const auto &name : dropped)
		if (const auto at = rings.find(name); at != rings.end())
			rings.erase(at);
	// A ring keeps the entries after the new s; one behind it, or of another incarnation, restarts at s.
	std::vector<Position> positions;
	for (const auto &layout : layouts) {
		auto &ring = rings[layout.name];
		const auto s = layout.end();
		if (ring.incarnation != layout.incarnation || ring.head < s)
			ring = {layout.incarnation, s, {}};
		else
			ring.entries.erase(ring.entries.begin(), ring.entries.upper_bound(s));
		positions.push_back({layout.name, d::NamespaceCursor{cluster, layout.incarnation, ring.head}});
	}
	if (positions.empty())
		return stats;
	const auto body = upstream.frames(positions);
	RingSink sink(cluster, rings);
	d::SessionFramesDecoder decoder(sink);
	std::span<const char> input(body);
	auto budget = body.size();
	decoder.feed(input, budget);
	decoder.finish();
	for (auto &[name, entries] : sink.staged) {
		auto &ring = rings.find(name)->second;
		stats.entries += entries.size();
		ring.entries.merge(entries);
		ring.head = sink.heads[name];
	}
	return stats;
}

NamespaceRelay::NamespaceRelay(std::shared_ptr<kronuz::io::completion::PosixIO> io, d::Identity cluster,
							   Upstream &upstream, MirrorStore::Options options)
	: impl_(std::make_unique<Impl>(std::move(io), cluster, upstream, options)) {}
NamespaceRelay::~NamespaceRelay() = default;
const MirrorStore &NamespaceRelay::mirror() const noexcept { return impl_->store; }

SyncStats NamespaceRelay::sync(std::span<const std::string> names) {
	if (!std::is_sorted(names.begin(), names.end()) ||
		std::adjacent_find(names.begin(), names.end()) != names.end())
		throw std::invalid_argument("relay sync: names must be strictly increasing");
	SyncStats total;
	for (int i = 0;; ++i) {
		try {
			const auto stats = impl_->attempt(names);
			total.files.fetched_files += stats.files.fetched_files;
			total.files.fetched_bytes += stats.files.fetched_bytes;
			total.files.reused_files += stats.files.reused_files;
			total.files.collected_files += stats.files.collected_files;
			total.entries += stats.entries;
			return total;
		} catch (const UpstreamMoved &) {
			if (i + 1 == sync_attempts)
				throw;
		}
	}
}

std::optional<d::NamespaceCursor> NamespaceRelay::head(std::string_view name) const {
	const auto ring = impl_->rings.find(name);
	if (ring == impl_->rings.end())
		return std::nullopt;
	return d::NamespaceCursor{impl_->cluster, ring->second.incarnation, ring->second.head};
}

// A namespace failed at startup is still offered: its missing files are fetched by name from this
// relay's upstream when a downstream reads them, so the downstream keeps what it holds.
std::vector<NamespaceLayout> NamespaceRelay::layouts(std::span<const std::string> names) {
	std::vector<NamespaceLayout> out;
	for (const auto &name : names)
		if (const auto *layout = impl_->store.table().find(name))
			out.push_back(*layout);
	return out;
}

std::string NamespaceRelay::fetch(const MirrorFile &file) { return impl_->store.read(file, impl_->fetcher()); }

std::string NamespaceRelay::frames(std::span<const Position> positions) {
	std::vector<std::unique_ptr<MirrorHistory>> histories;
	std::vector<d::NamespaceRequest> requests;
	for (const auto &position : positions) {
		const auto *layout = impl_->store.table().find(position.name);
		if (!layout || !impl_->serves(*layout))
			continue;
		histories.push_back(std::make_unique<MirrorHistory>(*impl_, impl_->cluster, *layout, impl_->rings.at(position.name)));
		requests.push_back({position.name, histories.back().get(), position.cursor});
	}
	return d::encode_session_frames(std::nullopt, requests);
}
} // namespace detent::relay
