#include "detent_relay/namespace_relay.hh"
#include "delta_transfer.hh"
#include "history_format.hh"
#include "page_copy.hh"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace detent::relay {
namespace d = distribution;
namespace h = authority::history;
namespace {
constexpr std::size_t sync_attempts = 3;
// Generations the page window keeps per namespace, as the record ring's positions (D7.3).
constexpr std::size_t window_positions = 64;

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
// A mirrored file's changes, checked against the layout that names it.
h::Changes decode_changes(const NamespaceLayout &layout, const MirrorFile &file, std::string_view bytes) {
	auto block = file.kind == MirrorFile::Kind::Block || file.kind == MirrorFile::Kind::Section
					 ? h::decode_block(bytes, {})
					 : h::decode_base(bytes, {});
	if (block.name_space != layout.name || block.incarnation != layout.incarnation || block.first != file.first ||
		block.last != file.last)
		throw d::HistoryFailure("relay mirror: a file holds another range: " + file.name);
	return std::move(block.changes);
}
// Files read on the relay's thread, decoded on another: what a build reads its full copy from.
class HeldTiers final : public Tiers {
  public:
	std::map<std::string, std::string, std::less<>> bytes;
	h::Changes changes(const NamespaceLayout &layout, const MirrorFile &file) override {
		return decode_changes(layout, file, bytes.at(file.name));
	}
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
	// D7: per namespace, the newest generation and the page window it heads, the canonical full copy
	// at s, the D6 answers from window bases to the head (D2: a transfer, or null for records), the
	// build in flight, and the target whose build failed (records until the head moves past it).
	struct Pages {
		std::shared_ptr<const Generation> head;
		PageWindow window{window_positions};
		std::shared_ptr<const CanonicalCopy> copy;
		std::map<std::uint64_t, std::shared_ptr<const std::vector<char>>> answers;
		std::uint64_t answer_bytes = 0;
		std::optional<d::NamespaceCursor> building, failed;
	};
	std::map<std::string, Pages, std::less<>> pages;
	PagesStats pages_stats;
	// What the runner's tasks hand back, shared with them so a task may outlive the relay.
	struct Landed {
		struct Build {
			std::string name;
			d::NamespaceCursor at;
			std::shared_ptr<const Generation> head; // null: the build failed
			std::shared_ptr<const CanonicalCopy> copy; // a new full copy, or null
			bool incremental = false, relocated = false;
		};
		std::mutex mutex;
		std::vector<Build> builds;
		std::atomic<bool> cancelled{false};
	};
	std::shared_ptr<Landed> landed = std::make_shared<Landed>();
	BuildRunner runner;

	Impl(std::shared_ptr<kronuz::io::completion::PosixIO> io, d::Identity cluster, Upstream &upstream,
		 MirrorStore::Options options, BuildRunner runner)
		: store(std::move(io), options), cluster(cluster), upstream(upstream), runner(std::move(runner)) {
		for (const auto &layout : store.table().entries)
			rings[layout.name] = {layout.incarnation, layout.end(), {}};
	}
	~Impl() { landed->cancelled.store(true, std::memory_order_relaxed); }
	MirrorFetch fetcher() {
		return [this](std::string_view name_space, const MirrorFile &file) { return upstream.fetch(name_space, file); };
	}
	bool serves(const NamespaceLayout &layout) const {
		const auto failed = store.failed();
		return std::find(failed.begin(), failed.end(), layout.name) == failed.end();
	}
	h::Changes changes(const NamespaceLayout &layout, const MirrorFile &file) override {
		return decode_changes(layout, file, store.read(file, fetcher()));
	}
	SyncStats attempt(std::span<const std::string> names);
	std::optional<std::string> answer(std::span<const Position>, bool changed_only);
	std::shared_ptr<const CanonicalCopy> copy(const std::string &name, const d::NamespaceHistory &, bool &wait);
	// A pages delta to the head: its transfer, and the head it names.
	struct Delta {
		std::shared_ptr<const std::vector<char>> transfer;
		std::shared_ptr<const Generation> head;
	};
	std::optional<Delta> delta(const std::string &name, const d::NamespaceHistory &, const d::NamespaceCursor &base,
							   bool &wait);
	void build(const std::string &name, const d::NamespaceHistory &, Pages &, bool relocate = false);
	bool collect();
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
		if (kind == d::FrameKind::Pages)
			throw std::runtime_error("relay: the upstream sent pages; a relay mirrors records");
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
	// A layout is a function of its upstream seal: an older answer, or another one at the same seal,
	// is a voter that lags, and neither adopts nor drops anything.
	const auto answer = upstream.layouts(names);
	const auto &layouts = answer.entries;
	const auto &table = store.table();
	if (answer.seal < table.seal)
		throw UpstreamBehind("relay: the upstream answered a seal older than the mirror's");
	std::vector<std::string> dropped;
	for (const auto &name : names)
		if (std::none_of(layouts.begin(), layouts.end(), [&](const auto &layout) { return layout.name == name; })) {
			if (const auto *held = table.find(name); held && held->seal >= answer.seal)
				throw UpstreamBehind("relay: the upstream lacks a namespace at the mirror's seal: " + name);
			dropped.push_back(name);
		}
	for (const auto &layout : layouts) {
		if (!std::binary_search(names.begin(), names.end(), layout.name) || layout.seal > answer.seal)
			throw std::runtime_error("relay: the upstream sent a namespace it was not asked for, or past its seal");
		if (const auto *held = table.find(layout.name);
			held && (held->seal > layout.seal || (held->seal == layout.seal && *held != layout)))
			throw UpstreamBehind("relay: the upstream's layout is older than the mirror's: " + layout.name);
	}
	SyncStats stats;
	stats.files = store.adopt(answer.seal, layouts, dropped, fetcher());
	for (const auto &name : dropped) {
		if (const auto at = rings.find(name); at != rings.end())
			rings.erase(at);
		if (const auto at = pages.find(name); at != pages.end())
			pages.erase(at);
	}
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
							   Upstream &upstream, MirrorStore::Options options, BuildRunner runner)
	: impl_(std::make_unique<Impl>(std::move(io), cluster, upstream, options, std::move(runner))) {}
NamespaceRelay::~NamespaceRelay() = default;
const MirrorStore &NamespaceRelay::mirror() const noexcept { return impl_->store; }

SyncStats NamespaceRelay::sync(std::span<const std::string> names) {
	if (!std::is_sorted(names.begin(), names.end()) ||
		std::adjacent_find(names.begin(), names.end()) != names.end())
		throw std::invalid_argument("relay sync: names must be strictly increasing");
	SyncStats total;
	for (std::size_t moved = 0, behind = 0;;) {
		try {
			const auto stats = impl_->attempt(names);
			total.files.fetched_files += stats.files.fetched_files;
			total.files.fetched_bytes += stats.files.fetched_bytes;
			total.files.reused_files += stats.files.reused_files;
			total.files.collected_files += stats.files.collected_files;
			total.entries += stats.entries;
			return total;
		} catch (const UpstreamMoved &) {
			if (++moved == sync_attempts)
				throw;
		} catch (const UpstreamBehind &) {
			// A lagging source: the next one may be current.
			if (++behind >= impl_->upstream.sources())
				throw;
			impl_->upstream.skip();
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
MirrorTable NamespaceRelay::layouts(std::span<const std::string> names) {
	const auto &table = impl_->store.table();
	MirrorTable out{0, table.seal, {}};
	for (const auto &name : names)
		if (const auto *layout = table.find(name))
			out.entries.push_back(*layout);
	return out;
}

// A file of `name_space`'s layout only: a downstream reads the namespaces it was granted.
std::string NamespaceRelay::fetch(std::string_view name_space, const MirrorFile &file) {
	const auto *layout = impl_->store.table().find(name_space);
	if (!layout || std::find(layout->files.begin(), layout->files.end(), file) == layout->files.end())
		throw std::invalid_argument("relay fetch: not a file of the namespace's layout: " + file.name);
	return impl_->store.read(file, impl_->fetcher());
}

std::string NamespaceRelay::frames(std::span<const Position> positions) {
	return impl_->answer(positions, false).value();
}
std::optional<std::string> NamespaceRelay::changes(std::span<const Position> positions) {
	return impl_->answer(positions, true);
}
std::optional<std::string> NamespaceRelay::Impl::answer(std::span<const Position> positions, bool changed_only) {
	std::vector<std::unique_ptr<MirrorHistory>> histories;
	std::deque<d::PagesAnswer> transfers;
	// Every copy, transfer and generation a pages frame names stays alive until the body is encoded,
	// whatever a build that lands during this answer does to the window.
	std::vector<std::shared_ptr<const void>> owned;
	std::vector<d::NamespaceRequest> requests;
	for (const auto &position : positions) {
		const auto *layout = store.table().find(position.name);
		if (!layout || !serves(*layout))
			continue;
		histories.push_back(std::make_unique<MirrorHistory>(*this, cluster, *layout, rings.at(position.name)));
		const auto &history = *histories.back();
		d::NamespaceRequest request{position.name, &history, position.cursor};
		if (position.pages) {
			const auto chosen = d::select_answer(history, position.cursor);
			bool wait = false;
			if (chosen.kind == d::AnswerKind::FullCopy) {
				if (auto full = copy(position.name, history, wait)) {
					request.pages = &transfers.emplace_back(d::PagesAnswer{full->root, 0, full->at, full->transfer});
					owned.push_back(std::move(full));
					++pages_stats.full_copies;
				}
			} else if (chosen.kind == d::AnswerKind::Entries || chosen.kind == d::AnswerKind::Blocks) {
				if (auto moved = delta(position.name, history, *position.cursor, wait)) {
					request.pages = &transfers.emplace_back(d::PagesAnswer{
						moved->head->root(), position.cursor->position, moved->head->at(), *moved->transfer});
					owned.push_back(std::move(moved->transfer));
					owned.push_back(std::move(moved->head));
				}
			}
			if (wait)
				continue; // no frame until the build lands, as for a Current answer
		}
		requests.push_back(request);
	}
	d::SessionFramesEncoder encoder(std::nullopt, requests);
	if (changed_only && std::all_of(encoder.answers().begin(), encoder.answers().end(), [](const d::Answer &answer) {
			return answer.kind == d::AnswerKind::Current || answer.kind == d::AnswerKind::Ahead;
		}))
		return std::nullopt;
	std::string body;
	while (!encoder.step(body, body.size() + 65536)) {
	}
	return body;
}

// Starts the build at the namespace's head (D7.2): over the window's head when the records after it
// reach it (dense-pages D7), else in full from the records full copy. Nothing while a build is in
// flight or failed at this head, and nothing at position 0, which no pages frame can name. A new
// head becomes a new canonical copy (D7.4), relocated into seal order, when the delta from the
// copy's position stops being smaller than a fresh copy, when that position left the window
// (`relocate`), or when its segments hold more dead bytes than live ones. With `relocate`, a head
// already built is relocated alone. The files the records read are read here, on the relay's
// thread, with the layout and ring as they are now; decoding, the records and the build run on the
// runner's.
void NamespaceRelay::Impl::build(const std::string &name, const d::NamespaceHistory &history, Pages &held,
								 bool relocate) {
	const auto head = history.head();
	if (!head.position || held.building || held.failed == head)
		return;
	const bool current = held.head && held.head->at() == head;
	if (current && !relocate)
		return;
	std::shared_ptr<const Generation> base;
	if (current)
		base = held.head;
	else if (held.head && held.head->at().same_identity(head) && held.head->at().position < head.position) {
		const auto kind = d::select_answer(history, held.head->at()).kind;
		if (kind == d::AnswerKind::Entries || kind == d::AnswerKind::Blocks)
			base = held.head;
	}
	std::shared_ptr<const Generation> at_copy;
	if (held.copy && base && held.copy->at.same_identity(head))
		if (const auto *entry = held.window.at(held.copy->at.position))
			at_copy = std::static_pointer_cast<const Generation>(entry->owner);
	struct Input {
		NamespaceLayout layout;
		Ring ring;
		HeldTiers tiers;
	};
	auto input = std::make_shared<Input>(Input{*store.table().find(name), rings.at(name), {}});
	const auto &files = input->layout.files;
	if (base && !current) {
		for (const auto &file : files)
			if ((file.kind == MirrorFile::Kind::Block || file.kind == MirrorFile::Kind::Section) &&
				file.last > base->at().position)
				input->tiers.bytes.emplace(file.name, store.read(file, fetcher()));
	} else if (!base) {
		const bool cached = !files.empty() && files.back().kind == MirrorFile::Kind::Copy;
		for (auto file = cached ? files.end() - 1 : files.begin(); file != files.end(); ++file)
			input->tiers.bytes.emplace(file->name, store.read(*file, fetcher()));
	}
	auto task = [landed = landed, cluster = cluster, name, head, input, base, at_copy, copy = held.copy, current,
				 relocate] {
		Landed::Build out{name, head, nullptr, nullptr};
		try {
			auto built = base;
			if (!current) {
				const MirrorHistory history(input->tiers, cluster, input->layout, input->ring);
				const d::NamespaceRequest request{name, &history, base ? std::optional(base->at()) : std::nullopt};
				built = build_generation(d::encode_session_frames(std::nullopt, {&request, 1}), base,
										 &landed->cancelled);
				out.incremental = base != nullptr;
			}
			// A cold build's files are canonical: it is its own full copy.
			bool fresh = !base;
			if (base) {
				fresh = relocate || !copy || !at_copy || 2 * built->page_bytes() < built->physical_bytes();
				if (!fresh && at_copy != built)
					fresh = merkle_diff(built->tree(), built->pages(), at_copy->tree(), at_copy->pages(),
										built->limits())
								.bytes >= built->page_bytes();
				if (fresh) {
					built = relay::relocate(*built, &landed->cancelled);
					out.relocated = true;
				}
			}
			if (fresh)
				out.copy = full_copy(*built);
			out.head = std::move(built);
		} catch (const std::exception &) {
		}
		std::lock_guard lock(landed->mutex);
		landed->builds.push_back(std::move(out));
	};
	held.building = head;
	if (runner)
		runner(std::move(task));
	else
		task();
	collect();
}

bool NamespaceRelay::Impl::collect() {
	std::vector<Landed::Build> builds;
	{
		std::lock_guard lock(landed->mutex);
		builds.swap(landed->builds);
	}
	for (auto &each : builds) {
		const auto found = pages.find(each.name);
		if (found == pages.end())
			continue; // dropped while it built
		auto &held = found->second;
		if (held.building == each.at)
			held.building.reset();
		if (!each.head || each.head->at() != each.at) {
			held.failed = each.at;
			++pages_stats.build_failures;
			continue;
		}
		const auto ring = rings.find(each.name);
		if (ring == rings.end() || ring->second.incarnation != each.at.incarnation)
			continue;
		if (held.head && held.head->at().incarnation != each.at.incarnation) {
			held.window.clear();
			held.head.reset();
			held.copy.reset();
		}
		if (held.head && held.head->at().position > each.at.position)
			continue;
		if (held.head && held.head->at() == each.at) {
			if (!each.relocated)
				continue;
			held.window.rebase(window_entry(each.head));
		} else {
			held.window.advance(window_entry(each.head), each.head->page_bytes());
			held.answers.clear();
			held.answer_bytes = 0;
			++pages_stats.builds;
			pages_stats.incremental += each.incremental;
		}
		held.head = std::move(each.head);
		if (each.copy)
			held.copy = std::move(each.copy);
		pages_stats.relocations += each.relocated;
	}
	return !builds.empty();
}

// The full copy as pages: the canonical copy at s, while the history still answers a delta from s.
// The build at the head starts when s is older, relocating a copy whose position left the window.
// Nothing while no copy has landed: `wait` when a build is in flight, records when it failed.
std::shared_ptr<const CanonicalCopy> NamespaceRelay::Impl::copy(const std::string &name,
																const d::NamespaceHistory &history, bool &wait) {
	auto &held = pages[name];
	build(name, history, held, held.copy && held.head && !held.window.at(held.copy->at.position));
	const auto head = history.head();
	if (held.copy && held.copy->at.incarnation == head.incarnation && held.copy->at.position <= head.position &&
		d::select_answer(history, held.copy->at).kind != d::AnswerKind::FullCopy)
		return held.copy;
	wait = held.building.has_value();
	return nullptr;
}

// D6 for a base the page window holds: the transfer to the head when it is smaller as sent than the
// records answer from that base, decided once per base and head (D2). Below the head, the delta waits
// (`wait`) for the build at the head unless D6 rule 1 already picks records against a lower bound of
// the pages: every delta carries a root page, estimated by the newest build's.
std::optional<NamespaceRelay::Impl::Delta> NamespaceRelay::Impl::delta(const std::string &name,
																	   const d::NamespaceHistory &history,
																	   const d::NamespaceCursor &base, bool &wait) {
	const auto found = pages.find(name);
	if (found == pages.end() || !found->second.head)
		return std::nullopt;
	auto &held = found->second;
	if (held.head->at().incarnation != base.incarnation || !held.window.at(base.position))
		return std::nullopt;
	const d::NamespaceRequest request{name, &history, base};
	if (held.head->at() != history.head()) {
		const auto records = d::encode_session_frames(std::nullopt, {&request, 1});
		const bool may_win = held.head->tree().length <= delivery::shortcut_ratio * records.size();
		if (may_win)
			build(name, history, held);
		if (held.head->at() != history.head()) {
			wait = may_win && held.building == history.head();
			if (!wait)
				++pages_stats.delta_records;
			return std::nullopt;
		}
	}
	auto head = held.head;
	if (base.position >= head->at().position)
		return std::nullopt;
	auto known = held.answers.find(base.position);
	if (known == held.answers.end()) {
		const auto diff = held.window.delta(base.position);
		if (!diff)
			return std::nullopt;
		auto transfer = std::make_shared<const std::vector<char>>(delta_transfer(*diff, *head));
		const auto records = d::encode_session_frames(std::nullopt, {&request, 1});
		if (delivery::choose_encoding(*transfer, records).encoding != delivery::Encoding::Pages)
			transfer.reset();
		const auto size = transfer ? transfer->size() : 0;
		if (held.answer_bytes + size > head->page_bytes()) {
			held.answers.clear();
			held.answer_bytes = 0;
		}
		held.answer_bytes += size;
		known = held.answers.emplace(base.position, std::move(transfer)).first;
	}
	if (!known->second) {
		++pages_stats.delta_records;
		return std::nullopt;
	}
	++pages_stats.deltas;
	return Delta{known->second, std::move(head)};
}
bool NamespaceRelay::collect() { return impl_->collect(); }
const PagesStats &NamespaceRelay::pages() const noexcept { return impl_->pages_stats; }
} // namespace detent::relay
