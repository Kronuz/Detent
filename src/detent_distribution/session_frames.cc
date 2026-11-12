#include "detent_distribution/session_frames.hh"
#include "detent/validation.hh"
#include "detent_distribution/grants.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/wire_encoder.hh"
#include <algorithm>

namespace detent::distribution {
namespace m = messagepack;
namespace {
void source(bool condition, const char *message) {
	if (!condition)
		throw HistoryFailure(message);
}
void check_shape(const NamespaceHistory &history) {
	const auto head = history.head();
	const auto p = history.cut(), s = history.ring_start();
	source(head.cluster != Identity{} && head.incarnation != Identity{}, "namespace history has no identity");
	source(p <= s && s <= head.position, "namespace history tiers out of order");
	auto at = p;
	bool unnamed = false;
	for (const auto &span : history.spans()) {
		source(span.first == at + 1 && span.first <= span.last, "namespace history spans are not contiguous");
		source(!(span.named && unnamed), "a named span follows a level-0 section");
		unnamed = unnamed || !span.named;
		at = span.last;
	}
	source(at == s, "namespace history spans do not reach the ring");
}
} // namespace

Answer select_answer(const NamespaceHistory &history, const std::optional<NamespaceCursor> &store) {
	check_shape(history);
	const auto head = history.head();
	if (!store || !store->same_identity(head))
		return {AnswerKind::FullCopy};
	const auto r = store->position;
	if (r > head.position)
		return {AnswerKind::Ahead, r};
	if (r == head.position)
		return {AnswerKind::Current, r};
	if (r >= history.ring_start())
		return {AnswerKind::Entries, r};
	if (r < history.cut())
		return {AnswerKind::FullCopy};
	const auto spans = history.spans();
	auto holding = std::partition_point(spans.begin(), spans.end(),
										[&](const HistorySpan &span) { return span.last < r + 1; });
	return {AnswerKind::Blocks, r, static_cast<std::size_t>(holding - spans.begin())};
}

namespace {
void put_cursor(std::string &out, const NamespaceCursor &cursor) {
	m::array(out, 3);
	m::string(out, {cursor.cluster.data(), cursor.cluster.size()}, true);
	m::string(out, {cursor.incarnation.data(), cursor.incarnation.size()}, true);
	m::unsigned_integer(out, cursor.position);
}
void put_record(std::string &out, const HistoryRecord &record) {
	m::array(out, 3);
	m::string(out, record.key);
	m::unsigned_integer(out, record.revision);
	if (!record.value) {
		out.push_back('\xc0');
		return;
	}
	const auto &value = *record.value;
	detail::wire::definition(out, WireDefinition{value.default_value, {0, value.overrides.size()},
												 value.rollout_pct, value.rollout_value});
	for (const auto &[group, override_value] : value.overrides)
		detail::wire::override_row(out, group, override_value);
}
// Checks a source reader's records against what its tier promises.
class CheckedReader final : public HistoryReader {
  public:
	enum class Mode { Span, FullCopy, Ring };
	CheckedReader(std::unique_ptr<HistoryReader> input, Mode mode, std::uint64_t first, std::uint64_t last)
		: input_(std::move(input)), mode_(mode), first_(first), last_(last) {
		source(static_cast<bool>(input_), "namespace history reader missing");
	}
	const HistoryRecord *next() override {
		const auto *record = input_->next();
		if (!record)
			return nullptr;
		source(record->revision >= first_ && record->revision <= last_ && record->revision,
			   "namespace history record outside its tier");
		source(mode_ != Mode::FullCopy || record->value, "a full copy holds a tombstone");
		const bool same_group = mode_ != Mode::Ring || (seen_ && record->revision == revision_);
		if (mode_ == Mode::Ring)
			source(!seen_ || record->revision >= revision_, "namespace history ring out of order");
		source(!seen_ || !same_group || key_ < record->key, "namespace history keys out of order");
		key_.assign(record->key);
		revision_ = record->revision;
		seen_ = true;
		return record;
	}

  private:
	std::unique_ptr<HistoryReader> input_;
	Mode mode_;
	std::uint64_t first_, last_, revision_ = 0;
	std::string key_;
	bool seen_ = false;
};
// Level-0 sections merged at answer time (D6 "Answers"): key order, the youngest write wins.
class MergedReader final : public HistoryReader {
  public:
	explicit MergedReader(std::vector<std::unique_ptr<HistoryReader>> inputs) : inputs_(std::move(inputs)) {
		current_.reserve(inputs_.size());
		for (auto &input : inputs_)
			current_.push_back(input->next());
	}
	const HistoryRecord *next() override {
		if (emitted_)
			for (std::size_t i = 0; i < inputs_.size(); ++i)
				if (current_[i] && current_[i]->key == key_)
					current_[i] = inputs_[i]->next();
		const HistoryRecord *best = nullptr;
		for (const auto *record : current_) // oldest first, so a later equal key is younger
			if (record && (!best || record->key <= best->key))
				best = record;
		emitted_ = best != nullptr;
		if (best)
			key_.assign(best->key);
		return best;
	}

  private:
	std::vector<std::unique_ptr<HistoryReader>> inputs_;
	std::vector<const HistoryRecord *> current_;
	std::string key_;
	bool emitted_ = false;
};
struct PiecePlan {
	PieceKind kind;
	std::uint64_t first = 0, last = 0;
	std::vector<std::size_t> spans; // Block, Span: the spans it reads, oldest first
};
struct FramePlan {
	std::string_view name;
	const NamespaceHistory *history = nullptr;
	Answer answer;
	NamespaceCursor head;
	FrameKind kind = FrameKind::Entries;
	std::vector<PiecePlan> pieces; // before the ring's entries
	std::uint64_t ring_after = 0;  // the ring's entries (ring_after, head] follow the pieces
	std::uint64_t piece_count() const noexcept { return pieces.size() + (head.position - ring_after); }
};
FramePlan plan(const NamespaceRequest &request, const Answer &answer) {
	const auto &history = *request.history;
	FramePlan frame{request.name, &history, answer, history.head()};
	const auto spans = history.spans();
	switch (answer.kind) {
	case AnswerKind::Entries:
		frame.kind = FrameKind::Entries;
		frame.ring_after = answer.after;
		break;
	case AnswerKind::FullCopy:
		frame.kind = FrameKind::FullCopy;
		frame.pieces.push_back({PieceKind::FullCopy, 0, history.ring_start(), {}});
		frame.ring_after = history.ring_start();
		break;
	case AnswerKind::Blocks: {
		frame.kind = FrameKind::Blocks;
		auto index = answer.span;
		for (; index < spans.size() && spans[index].named; ++index) {
			const auto &span = spans[index];
			const bool whole = span.first > answer.after; // only the span holding R + 1 may be trimmed
			frame.pieces.push_back({whole ? PieceKind::Block : PieceKind::Span, span.first, span.last, {index}});
		}
		if (index < spans.size()) {
			PiecePlan merged{PieceKind::Span, spans[index].first, spans.back().last, {}};
			for (; index < spans.size(); ++index)
				merged.spans.push_back(index);
			frame.pieces.push_back(std::move(merged));
		}
		frame.ring_after = history.ring_start();
		break;
	}
	case AnswerKind::Current:
	case AnswerKind::Ahead:
		break;
	}
	return frame;
}
} // namespace

struct SessionFramesEncoder::Impl {
	enum class Phase { Header, Frame, Piece, Records, Ring, Done };
	std::optional<DirectoryUpdate> directory;
	std::vector<Answer> answers;
	std::vector<FramePlan> frames;
	Phase phase = Phase::Header;
	std::size_t frame = 0, piece = 0;
	std::unique_ptr<HistoryReader> reader;
	std::uint64_t after = 0;   // the open piece's records are above it
	std::uint64_t position = 0; // Ring: the open entry, 0 before the first
	void header(std::string &out) {
		m::array(out, 3);
		m::unsigned_integer(out, session_frames_version);
		if (!directory)
			out.push_back('\xc0');
		else {
			m::array(out, 4);
			m::string(out, {directory->to.cluster.data(), directory->to.cluster.size()}, true);
			m::unsigned_integer(out, directory->from);
			m::unsigned_integer(out, directory->to.position);
			m::array(out, static_cast<std::uint32_t>(directory->entries.size()));
			for (const auto &entry : directory->entries) {
				m::array(out, 3);
				m::string(out, entry.name);
				m::string(out, {entry.incarnation.data(), entry.incarnation.size()}, true);
				out.push_back(entry.live ? '\xc3' : '\xc2');
			}
		}
		m::unsigned_integer(out, frames.size());
		phase = frames.empty() ? Phase::Done : Phase::Frame;
	}
	void open_frame(std::string &out) {
		const auto &current = frames[frame];
		m::array(out, 5);
		m::string(out, current.name);
		m::unsigned_integer(out, static_cast<std::uint8_t>(current.kind));
		m::unsigned_integer(out, current.answer.after);
		put_cursor(out, current.head);
		m::unsigned_integer(out, current.piece_count());
		piece = 0;
		phase = Phase::Piece;
	}
	void open_piece(std::string &out) {
		auto &current = frames[frame];
		const auto &history = *current.history;
		if (piece == current.pieces.size()) {
			if (current.ring_after == current.head.position) {
				next_frame();
				return;
			}
			reader = std::make_unique<CheckedReader>(history.read_ring(current.ring_after),
													 CheckedReader::Mode::Ring, current.ring_after + 1,
													 current.head.position);
			position = 0;
			phase = Phase::Ring;
			return;
		}
		const auto &plan = current.pieces[piece];
		m::array(out, 3);
		m::unsigned_integer(out, static_cast<std::uint8_t>(plan.kind));
		m::unsigned_integer(out, plan.first);
		m::unsigned_integer(out, plan.last);
		after = current.answer.after;
		if (plan.kind == PieceKind::FullCopy)
			reader = std::make_unique<CheckedReader>(history.read_full_copy(), CheckedReader::Mode::FullCopy, 1,
													 plan.last);
		else {
			const auto spans = history.spans();
			std::vector<std::unique_ptr<HistoryReader>> inputs;
			for (auto index : plan.spans)
				inputs.push_back(std::make_unique<CheckedReader>(history.read_span(index),
																 CheckedReader::Mode::Span, spans[index].first,
																 spans[index].last));
			reader = inputs.size() == 1 ? std::move(inputs.front())
										: std::make_unique<MergedReader>(std::move(inputs));
		}
		phase = Phase::Records;
	}
	void next_frame() {
		reader.reset();
		phase = ++frame == frames.size() ? Phase::Done : Phase::Frame;
	}
	void records(std::string &out, std::size_t bytes) {
		while (out.size() < bytes) {
			const auto *record = reader->next();
			if (!record) {
				out.push_back('\xc0');
				reader.reset();
				++piece;
				phase = Phase::Piece;
				return;
			}
			if (record->revision > after)
				put_record(out, *record);
		}
	}
	void ring(std::string &out, std::size_t bytes) {
		const auto &current = frames[frame];
		while (out.size() < bytes) {
			const auto *record = reader->next();
			if (!record || record->revision != position) {
				if (position)
					out.push_back('\xc0');
				if (!record) {
					source(position == current.head.position, "namespace history ring ends before head");
					next_frame();
					return;
				}
				source(record->revision == (position ? position : current.ring_after) + 1,
					   "namespace history ring skips a position");
				position = record->revision;
				m::array(out, 3);
				m::unsigned_integer(out, static_cast<std::uint8_t>(PieceKind::Entry));
				m::unsigned_integer(out, position);
				m::unsigned_integer(out, position);
			}
			put_record(out, *record);
		}
	}
};
SessionFramesEncoder::SessionFramesEncoder(std::optional<DirectoryUpdate> directory,
										   std::span<const NamespaceRequest> requests)
	: impl_(std::make_unique<Impl>()) {
	if (directory) {
		if (directory->to.cluster == Identity{} || directory->from > directory->to.position ||
			directory->entries.size() > directory->to.position - directory->from)
			throw std::invalid_argument("invalid directory update");
		for (const auto &entry : directory->entries)
			if (!valid_namespace_name(entry.name) || entry.incarnation == Identity{})
				throw std::invalid_argument("invalid directory entry");
	}
	impl_->directory = std::move(directory);
	std::string_view previous;
	for (const auto &request : requests) {
		if (!request.history || !valid_namespace_name(request.name) || (!previous.empty() && request.name <= previous))
			throw std::invalid_argument("session frames need valid names in strictly increasing order");
		previous = request.name;
		const auto answer = select_answer(*request.history, request.store);
		impl_->answers.push_back(answer);
		if (answer.kind != AnswerKind::Current && answer.kind != AnswerKind::Ahead)
			impl_->frames.push_back(plan(request, answer));
	}
}
SessionFramesEncoder::~SessionFramesEncoder() = default;
std::span<const Answer> SessionFramesEncoder::answers() const noexcept { return impl_->answers; }
bool SessionFramesEncoder::step(std::string &out, std::size_t bytes) {
	auto &p = *impl_;
	while (p.phase != Impl::Phase::Done && out.size() < bytes) {
		switch (p.phase) {
		case Impl::Phase::Header:
			p.header(out);
			break;
		case Impl::Phase::Frame:
			p.open_frame(out);
			break;
		case Impl::Phase::Piece:
			p.open_piece(out);
			break;
		case Impl::Phase::Records:
			p.records(out, bytes);
			break;
		case Impl::Phase::Ring:
			p.ring(out, bytes);
			break;
		case Impl::Phase::Done:
			break;
		}
	}
	return p.phase == Impl::Phase::Done;
}
std::string encode_session_frames(std::optional<DirectoryUpdate> directory,
								  std::span<const NamespaceRequest> requests) {
	SessionFramesEncoder encoder(std::move(directory), requests);
	std::string out;
	while (!encoder.step(out, out.size() + 65536)) {
	}
	return out;
}

namespace {
void wire(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
} // namespace
struct SessionFramesDecoder::Impl {
	enum class S {
		Body,
		Version,
		Directory,
		DirectoryCluster,
		DirectoryFrom,
		DirectoryTo,
		DirectoryEntries,
		Entry,
		EntryName,
		EntryIncarnation,
		EntryLive,
		FrameCount,
		Frame,
		FrameName,
		FrameKindField,
		FrameAfter,
		Cursor,
		CursorCluster,
		CursorIncarnation,
		CursorPosition,
		PieceCount,
		Piece,
		PieceKindField,
		PieceFirst,
		PieceLast,
		Record,
		RecordKey,
		RecordRevision,
		Definition,
		DefaultValue,
		RolloutPercent,
		RolloutValue,
		Overrides,
		Override,
		OverrideName,
		OverrideMode,
		OverrideValue,
		Done
	};
	FrameSink &sink;
	Limits limits;
	m::Decoder tokens;
	S state = S::Body;
	DirectoryUpdate directory;
	std::uint64_t remaining = 0, frames = 0, records = 0, strings = 0;
	// The open frame.
	std::string name, previous_name;
	FrameKind kind = FrameKind::Entries;
	NamespaceCursor to;
	std::uint64_t after = 0, pieces = 0, at = 0;
	bool copy_seen = false, spans_seen = false, entries_seen = false;
	// The open piece and record.
	PieceKind piece_kind = PieceKind::Entry;
	std::uint64_t first = 0, last = 0, revision = 0, overrides = 0, definition_bytes = 0;
	std::string key, previous_key, group, previous_group;
	bool piece_records = false;
	KnobDef value;
	std::uint64_t mode = 0;
	Impl(FrameSink &target, Limits bounds)
		: sink(target), limits(bounds),
		  tokens(m::Limits{limits.bytes, 16384, 32, std::max<std::uint32_t>(limits.records, limits.namespaces)}) {}
	static std::uint64_t unsigned_value(const m::Token &token) {
		wire(token.kind == m::Kind::Unsigned, "session frames expect an unsigned integer");
		return token.number;
	}
	static void array(const m::Token &token, std::uint64_t size) {
		wire(token.kind == m::Kind::Array && token.number == size, "session frames tuple arity");
	}
	static Identity identity(const m::Token &token) {
		Identity result{};
		wire(token.kind == m::Kind::Binary && token.bytes.size() == result.size(), "session frames identity");
		std::copy(token.bytes.begin(), token.bytes.end(), result.begin());
		wire(result != Identity{}, "session frames zero identity");
		return result;
	}
	std::string_view text(const m::Token &token) {
		wire(token.kind == m::Kind::String, "session frames expect a string");
		strings += token.bytes.size();
		wire(strings <= limits.string_bytes, "session frames string bytes bound");
		return token.bytes;
	}
	void frame_header_done() {
		const bool forward = kind != FrameKind::FullCopy;
		wire(forward ? after < to.position : after == 0, "session frame after its target");
		at = forward ? after : 0;
		copy_seen = spans_seen = entries_seen = false;
		sink.frame(name, kind, after, to);
	}
	void piece_header_done() {
		wire(pieces != 0, "session frame piece count exceeded");
		--pieces;
		wire(first <= last && last <= to.position, "session piece outside its frame");
		switch (piece_kind) {
		case PieceKind::Entry:
			wire(at < to.position, "session piece after its frame's target");
			wire(first == last && first == at + 1, "session entry is not the next position");
			wire(kind == FrameKind::Entries || (kind == FrameKind::Blocks ? spans_seen : copy_seen),
				 "session entry before its frame's first piece");
			entries_seen = true;
			break;
		case PieceKind::Block:
		case PieceKind::Span:
			wire(at < to.position, "session piece after its frame's target");
			wire(kind == FrameKind::Blocks && !entries_seen, "session span outside a blocks frame");
			wire(spans_seen ? first == at + 1 : first <= after + 1 && after + 1 <= last,
				 "session span is not contiguous");
			wire(piece_kind == PieceKind::Span || first == at + 1, "a whole block starts at the next position");
			spans_seen = true;
			break;
		case PieceKind::FullCopy:
			wire(kind == FrameKind::FullCopy && first == 0 && !copy_seen, "session full copy outside its place");
			copy_seen = true;
			break;
		}
		piece_records = false;
		previous_key.clear();
		sink.piece(piece_kind, first, last);
	}
	void piece_done() {
		wire(piece_kind != PieceKind::Entry || piece_records, "session entry without records");
		at = last;
		state = pieces ? S::Piece : end_frame();
	}
	S end_frame() {
		wire(at == to.position, "session frame ends short");
		sink.end_frame();
		return frames ? S::Frame : S::Done;
	}
	void record_header_done() {
		const auto colon = key.find(':');
		wire(is_valid_knob_name(key) && std::string_view(key).substr(0, colon) == name,
			 "session record key outside its namespace");
		wire(previous_key.empty() || previous_key < key, "session record keys out of order");
		const auto low = piece_kind == PieceKind::FullCopy ? 1 : std::max(first, after + 1);
		wire(revision >= low && revision <= last && (piece_kind != PieceKind::Entry || revision == first),
			 "session record revision outside its piece");
		wire(++records <= limits.records, "session frames record bound");
		previous_key = key;
		piece_records = true;
	}
	void record_done(const KnobDef *definition) {
		wire(definition || piece_kind != PieceKind::FullCopy, "a full copy holds a tombstone");
		if (definition)
			wire(definition_error(*definition) == nullptr, "invalid session definition");
		sink.record(key, revision, definition);
		state = S::Record;
	}
	void take(m::Token token) {
		switch (state) {
		case S::Body:
			array(token, 3);
			state = S::Version;
			break;
		case S::Version:
			wire(unsigned_value(token) == session_frames_version, "unsupported session frames version");
			state = S::Directory;
			break;
		case S::Directory:
			if (token.kind == m::Kind::Nil)
				state = S::FrameCount;
			else {
				array(token, 4);
				state = S::DirectoryCluster;
			}
			break;
		case S::DirectoryCluster:
			directory = {};
			directory.to.cluster = identity(token);
			state = S::DirectoryFrom;
			break;
		case S::DirectoryFrom:
			directory.from = unsigned_value(token);
			state = S::DirectoryTo;
			break;
		case S::DirectoryTo:
			directory.to.position = unsigned_value(token);
			wire(directory.from <= directory.to.position, "session directory goes backwards");
			state = S::DirectoryEntries;
			break;
		case S::DirectoryEntries:
			wire(token.kind == m::Kind::Array && token.number <= directory.to.position - directory.from &&
					 token.number <= limits.namespaces,
				 "session directory entry bound");
			remaining = token.number;
			state = S::Entry;
			if (!remaining) {
				sink.directory(directory);
				state = S::FrameCount;
			}
			break;
		case S::Entry:
			array(token, 3);
			state = S::EntryName;
			break;
		case S::EntryName:
			directory.entries.push_back({std::string(text(token))});
			wire(valid_namespace_name(directory.entries.back().name), "invalid session directory name");
			state = S::EntryIncarnation;
			break;
		case S::EntryIncarnation:
			directory.entries.back().incarnation = identity(token);
			state = S::EntryLive;
			break;
		case S::EntryLive:
			wire(token.kind == m::Kind::Boolean, "session directory live flag");
			directory.entries.back().live = token.number != 0;
			if (--remaining)
				state = S::Entry;
			else {
				sink.directory(directory);
				state = S::FrameCount;
			}
			break;
		case S::FrameCount:
			frames = unsigned_value(token);
			wire(frames <= limits.namespaces, "session frame count bound");
			state = frames ? S::Frame : S::Done;
			break;
		case S::Frame:
			array(token, 5);
			--frames;
			state = S::FrameName;
			break;
		case S::FrameName:
			previous_name = std::move(name);
			name = text(token);
			wire(valid_namespace_name(name) && (previous_name.empty() || previous_name < name),
				 "session frame names are not valid and in order");
			state = S::FrameKindField;
			break;
		case S::FrameKindField: {
			const auto value = unsigned_value(token);
			wire(value >= 1 && value <= 3, "unknown session frame kind");
			kind = static_cast<FrameKind>(value);
			state = S::FrameAfter;
			break;
		}
		case S::FrameAfter:
			after = unsigned_value(token);
			state = S::Cursor;
			break;
		case S::Cursor:
			array(token, 3);
			state = S::CursorCluster;
			break;
		case S::CursorCluster:
			to.cluster = identity(token);
			state = S::CursorIncarnation;
			break;
		case S::CursorIncarnation:
			to.incarnation = identity(token);
			state = S::CursorPosition;
			break;
		case S::CursorPosition:
			to.position = unsigned_value(token);
			state = S::PieceCount;
			break;
		case S::PieceCount:
			pieces = unsigned_value(token);
			frame_header_done();
			wire(pieces != 0, "session frame without pieces");
			state = S::Piece;
			break;
		case S::Piece:
			array(token, 3);
			state = S::PieceKindField;
			break;
		case S::PieceKindField: {
			const auto value = unsigned_value(token);
			wire(value >= 1 && value <= 4, "unknown session piece kind");
			piece_kind = static_cast<PieceKind>(value);
			state = S::PieceFirst;
			break;
		}
		case S::PieceFirst:
			first = unsigned_value(token);
			state = S::PieceLast;
			break;
		case S::PieceLast:
			last = unsigned_value(token);
			piece_header_done();
			state = S::Record;
			break;
		case S::Record:
			if (token.kind == m::Kind::Nil)
				piece_done();
			else {
				array(token, 3);
				state = S::RecordKey;
			}
			break;
		case S::RecordKey:
			key = text(token);
			state = S::RecordRevision;
			break;
		case S::RecordRevision:
			revision = unsigned_value(token);
			record_header_done();
			state = S::Definition;
			break;
		case S::Definition:
			if (token.kind == m::Kind::Nil)
				record_done(nullptr);
			else {
				array(token, 4);
				value = {};
				definition_bytes = 0;
				state = S::DefaultValue;
			}
			break;
		case S::DefaultValue:
			value.default_value = token.signed_value();
			state = S::RolloutPercent;
			break;
		case S::RolloutPercent:
			if (token.kind != m::Kind::Nil) {
				const auto percent = unsigned_value(token);
				wire(percent <= 100, "session rollout percentage");
				value.rollout_pct = static_cast<std::uint8_t>(percent);
			}
			state = S::RolloutValue;
			break;
		case S::RolloutValue:
			if (token.kind != m::Kind::Nil)
				value.rollout_value = token.signed_value();
			state = S::Overrides;
			break;
		case S::Overrides:
			wire(token.kind == m::Kind::Array && token.number <= limits.overrides_per_definition,
				 "session override bound");
			overrides = token.number;
			definition_bytes = definition_header_charge(value.rollout_pct.has_value(),
														value.rollout_value.has_value(), overrides);
			previous_group.clear();
			if (overrides)
				state = S::Override;
			else
				record_done(&value);
			break;
		case S::Override:
			array(token, 3);
			state = S::OverrideName;
			break;
		case S::OverrideName:
			group = text(token);
			wire(is_valid_group_key(group) && (previous_group.empty() || previous_group < group),
				 "session override keys are not valid and in order");
			state = S::OverrideMode;
			break;
		case S::OverrideMode:
			mode = unsigned_value(token);
			wire(mode <= 1, "unknown session override mode");
			state = S::OverrideValue;
			break;
		case S::OverrideValue: {
			GroupOverride entry;
			if (mode == 0)
				entry = token.signed_value();
			else {
				const auto percent = unsigned_value(token);
				wire(percent <= 100, "session group percentage");
				entry = GroupRollout{static_cast<std::uint8_t>(percent)};
			}
			definition_bytes += override_charge(group.size(), mode == 0);
			wire(definition_bytes <= limits.definition_bytes, "session definition bytes bound");
			value.overrides.emplace(group, entry);
			previous_group = std::move(group);
			if (--overrides)
				state = S::Override;
			else
				record_done(&value);
			break;
		}
		case S::Done:
			wire(false, "trailing session frames bytes");
		}
	}
};
SessionFramesDecoder::SessionFramesDecoder(FrameSink &sink, Limits limits)
	: impl_(std::make_unique<Impl>(sink, limits)) {}
SessionFramesDecoder::~SessionFramesDecoder() = default;
void SessionFramesDecoder::feed(std::span<const char> &input, std::size_t &budget) {
	auto &p = *impl_;
	while (!input.empty() && budget) {
		wire(p.state != Impl::S::Done, "trailing session frames bytes");
		if (auto token = p.tokens.next(input, budget))
			p.take(std::move(*token));
	}
}
void SessionFramesDecoder::finish() {
	impl_->tokens.finish();
	wire(impl_->state == Impl::S::Done, "incomplete session frames");
}
bool SessionFramesDecoder::complete() const noexcept { return impl_->state == Impl::S::Done; }
} // namespace detent::distribution
