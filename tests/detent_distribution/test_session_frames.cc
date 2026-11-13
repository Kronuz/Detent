// W5/W6 frames over a test history source (namespace-s6 A1): every R gets the tier D6 names,
// the frames rebuild the state at head with its revisions, and the bytes per answer are checked.
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/session_frames.hh"
#include "history_fixture.hh"
#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace d = detent::distribution;
namespace m = d::messagepack;
using detent::KnobDef;

using namespace history_fixture;
namespace {
void require(bool value, const std::string &what) {
	if (!value)
		throw std::runtime_error(what);
}
struct Piece {
	d::PieceKind kind;
	std::uint64_t first, last;
	std::vector<std::pair<std::string, Held>> records;
};
struct Frame {
	std::string name;
	d::FrameKind kind;
	std::uint64_t after;
	d::NamespaceCursor to;
	std::vector<Piece> pieces;
	bool ended = false;
};
struct Collect final : d::FrameSink {
	std::optional<d::DirectoryUpdate> directory_update;
	std::vector<Frame> frames;
	void directory(const d::DirectoryUpdate &value) override { directory_update = value; }
	void frame(std::string_view name, d::FrameKind kind, std::uint64_t after, const d::NamespaceCursor &to) override {
		frames.push_back({std::string(name), kind, after, to, {}});
	}
	void piece(d::PieceKind kind, std::uint64_t first, std::uint64_t last) override {
		frames.back().pieces.push_back({kind, first, last, {}});
	}
	void record(std::string_view key, std::uint64_t revision, const KnobDef *value) override {
		frames.back().pieces.back().records.emplace_back(
			std::string(key), Held{revision, value ? std::optional<KnobDef>(*value) : std::nullopt});
	}
	void end_frame() override { frames.back().ended = true; }
};
Collect decode(std::string_view body, std::size_t chunk = 1 << 20) {
	Collect sink;
	d::SessionFramesDecoder decoder(sink);
	auto input = std::span<const char>(body.data(), body.size());
	while (!input.empty()) {
		std::size_t budget = chunk;
		decoder.feed(input, budget);
	}
	decoder.finish();
	return sink;
}
bool rejected(std::string_view body) {
	try {
		decode(body);
	} catch (const std::invalid_argument &) {
		return true;
	} catch (const std::length_error &) {
		return true;
	}
	return false;
}
// The agent's side: apply a frame's pieces in order to the store at R.
State apply_frame(State store, const Frame &frame) {
	for (const auto &piece : frame.pieces) {
		if (piece.kind == d::PieceKind::FullCopy)
			store.clear();
		for (const auto &[key, held] : piece.records)
			if (held.value)
				store[key] = held;
			else
				store.erase(key); // a tombstone for a key the store lacks is a no-op
	}
	return store;
}
std::string hex(std::string_view bytes) {
	std::string out;
	char digits[3];
	for (unsigned char byte : bytes) {
		std::snprintf(digits, sizeof digits, "%02x", byte);
		out += digits;
	}
	return out;
}
// 40 transactions over ten keys of namespace `pay`, with deletes and re-inserts.
std::vector<Transaction> churn() {
	std::vector<Transaction> out;
	std::map<std::string, bool> live;
	std::uint64_t seed = 12345;
	auto next = [&] { return seed = seed * 6364136223846793005ull + 1442695040888963407ull, seed >> 33; };
	for (int position = 1; position <= 40; ++position) {
		Transaction tx;
		const auto count = 1 + next() % 3;
		for (std::uint64_t i = 0; i < count; ++i) {
			auto key = "pay:k" + std::to_string(next() % 10);
			if (std::any_of(tx.begin(), tx.end(), [&](auto &w) { return w.key == key; }))
				continue;
			if (live[key] && next() % 4 == 0) {
				tx.push_back({key, std::nullopt});
				live[key] = false;
			} else {
				tx.push_back({key, def(static_cast<std::int64_t>(position * 10 + i), position)});
				live[key] = true;
			}
		}
		out.push_back(std::move(tx));
	}
	return out;
}

void golden() {
	// x=1, y=2, delete x; one block [1, 2]; the store at 1 gets y trimmed from it, then the delete.
	std::vector<Transaction> txs{{{"a:x", def(1, 0)}}, {{"a:y", def(2, 0)}}, {{"a:x", std::nullopt}}};
	TestHistory history(txs, 0, {{1, 2, true}}, 2);
	d::NamespaceRequest request{"a", &history, d::NamespaceCursor{id(1), id(2), 1}};
	const auto body = d::encode_session_frames({}, std::span(&request, 1));
	const std::string ids = "c410" + std::string(32, '0').replace(0, 32, "01010101010101010101010101010101") +
							"c410" + "02020202020202020202020202020202";
	const auto expected = "9301c001"						   // version 1, no directory, one frame
						  "95a1610201" "93" + ids + "03" "02"   // "a", blocks, after 1, to 3, two pieces
						  "93030102" "93a3613a7902" "9402c0c090" "c0" // span [1, 2] above 1: y at 2
						  "93010303" "93a3613a7803" "c0" "c0";	// entry 3: x deleted
	require(hex(body) == expected, "golden blocks answer bytes: " + hex(body));
	require(body.size() == 76, "golden answer is 76 bytes");
	auto decoded = decode(body);
	require(decoded.frames.size() == 1 && decoded.frames[0].ended && decoded.frames[0].kind == d::FrameKind::Blocks,
			"golden decodes to one blocks frame");
	require(apply_frame(history.state_at(1), decoded.frames[0]) == history.state_at(3), "golden rebuilds head");
}

void tiers() {
	const auto txs = churn();
	// Base at 8 | blocks [9,16] [17,24] | level-0 [25,28] [29,30] | ring (30, 40]
	TestHistory history(txs, 8, {{9, 16, true}, {17, 24, true}, {25, 28, false}, {29, 30, false}}, 30);
	const auto head = history.state_at(40);
	std::ostringstream table;
	std::map<std::uint64_t, std::size_t> bytes;
	for (std::uint64_t r = 0; r <= 41; ++r) {
		d::NamespaceRequest request{"pay", &history, d::NamespaceCursor{id(1), id(2), r}};
		const auto expected = r == 40   ? d::AnswerKind::Current
							  : r > 40 ? d::AnswerKind::Ahead
							  : r >= 30 ? d::AnswerKind::Entries
							  : r >= 8  ? d::AnswerKind::Blocks
										: d::AnswerKind::FullCopy;
		d::SessionFramesEncoder encoder({}, std::span(&request, 1));
		require(encoder.answers()[0].kind == expected, "tier at R = " + std::to_string(r));
		const auto body = d::encode_session_frames({}, std::span(&request, 1));
		bytes[r] = body.size();
		auto decoded = decode(body);
		if (expected == d::AnswerKind::Current || expected == d::AnswerKind::Ahead) {
			require(decoded.frames.empty() && body == std::string("\x93\x01\xc0\x00", 4), "no frame at or above head");
			continue;
		}
		require(decoded.frames.size() == 1, "one frame");
		const auto &frame = decoded.frames[0];
		require(frame.to == d::NamespaceCursor{id(1), id(2), 40}, "frame targets head");
		const auto store = expected == d::AnswerKind::FullCopy ? State{} : history.state_at(r);
		require(apply_frame(store, frame) == head, "frame rebuilds head with its revisions at R = " + std::to_string(r));
		// Bytes per answer: every piece sends each key once; spans send exactly the keys above R.
		std::uint64_t records = 0;
		for (const auto &piece : frame.pieces) {
			records += piece.records.size();
			if (piece.kind == d::PieceKind::Entry)
				require(piece.records.size() == txs[piece.first - 1].size(), "an entry carries its transaction");
			if (piece.kind == d::PieceKind::Block)
				require(State(piece.records.begin(), piece.records.end()) == history.span(piece.first, piece.last),
						"a block is sent whole");
			if (piece.kind == d::PieceKind::Span) {
				State above;
				for (auto &[key, held] : history.span(piece.first, piece.last))
					if (held.revision > r)
						above[key] = held;
				require(State(piece.records.begin(), piece.records.end()) == above,
						"a span sends its keys above R once each, the youngest write");
			}
			if (piece.kind == d::PieceKind::FullCopy)
				require(State(piece.records.begin(), piece.records.end()) == history.state_at(30),
						"a full copy is the state at the ring's start");
		}
		if (expected == d::AnswerKind::Blocks) {
			// R + 1 in a block: that block (trimmed), each younger block, one merged level-0 piece;
			// R + 1 in level-0: one merged piece. Then the ring's 10 entries.
			const auto spans = r < 16 ? 3u : r < 24 ? 2u : 1u;
			require(frame.pieces.size() == spans + 10, "blocks frame pieces at R = " + std::to_string(r));
			require(frame.pieces[frame.pieces.size() - 11].kind == d::PieceKind::Span &&
						frame.pieces[frame.pieces.size() - 11].last == 30,
					"level-0 sections arrive as one merged piece ending at the ring");
			// Each named span above R is a whole Block; only the span holding R + 1 below it is a trimmed Span.
			for (std::size_t i = 0; i + 11 < frame.pieces.size(); ++i)
				require(frame.pieces[i].kind == (frame.pieces[i].first > r ? d::PieceKind::Block : d::PieceKind::Span),
						"named piece " + std::to_string(i) + " kind at R = " + std::to_string(r));
		}
		table << r << ':' << body.size() << '/' << records << ' ';
	}
	std::cout << "bytes/records per R: " << table.str() << '\n';
	// Pinned sizes, one per tier (the frames grammar and this churn fix them).
	require(bytes[0] == 545 && bytes[8] == 835 && bytes[24] == 528 && bytes[30] == 414 && bytes[39] == 73,
			"pinned bytes per answer changed: " + table.str());
	// A foreign incarnation or no store gets the full copy; another cluster too.
	for (const auto &store : {std::optional<d::NamespaceCursor>{}, std::optional(d::NamespaceCursor{id(1), id(9), 35}),
							  std::optional(d::NamespaceCursor{id(7), id(2), 35})}) {
		d::NamespaceRequest request{"pay", &history, store};
		d::SessionFramesEncoder encoder({}, std::span(&request, 1));
		require(encoder.answers()[0] == d::Answer{d::AnswerKind::FullCopy}, "foreign or missing store gets a copy");
	}
}

// A store of another incarnation is behind head (a full copy) or above it (Ahead: this source has
// not yet seen the re-creation, and positions continue across incarnations, D1).
void other_incarnation() {
	TestHistory history(churn(), 8, {{9, 16, true}, {17, 24, true}, {25, 28, false}, {29, 30, false}}, 30);
	for (std::uint64_t r : {0, 9, 30, 39, 40, 41, 90}) {
		const auto renewed = d::select_answer(history, d::NamespaceCursor{id(1), id(3), r});
		require(renewed.kind == (r > 40 ? d::AnswerKind::Ahead : d::AnswerKind::FullCopy),
				"another incarnation at R = " + std::to_string(r));
		d::NamespaceRequest request{"pay", &history, d::NamespaceCursor{id(1), id(3), r}};
		const auto body = d::encode_session_frames({}, std::span(&request, 1));
		require((r > 40) == (body == std::string("\x93\x01\xc0\x00", 4)), "no frame only above head");
		// Another cluster's positions say nothing about this one: always a full copy.
		require(d::select_answer(history, d::NamespaceCursor{id(4), id(2), r}).kind == d::AnswerKind::FullCopy,
				"another cluster at R = " + std::to_string(r));
	}
}

void chunking_and_directory() {
	const auto txs = churn();
	TestHistory pay(txs, 8, {{9, 16, true}, {17, 24, true}, {25, 30, false}}, 30);
	TestHistory empty({}, 0, {}, 0, id(3));
	std::vector<Transaction> other_txs{{{"zed:a", def(5, 0)}}};
	TestHistory zed(other_txs, 0, {}, 0, id(4));
	d::DirectoryUpdate directory{{id(1), 9}, 6, {{"zed", id(4), true}, {"gone", id(5), false}}};
	std::vector<d::NamespaceRequest> requests{{"empty", &empty, {}},
											  {"pay", &pay, d::NamespaceCursor{id(1), id(2), 12}},
											  {"zed", &zed, d::NamespaceCursor{id(1), id(4), 0}}};
	const auto whole = d::encode_session_frames(directory, requests);
	d::SessionFramesEncoder encoder(directory, requests);
	std::string stepped;
	while (!encoder.step(stepped, stepped.size() + 7)) {
	}
	require(stepped == whole, "bounded steps emit the same body");
	auto decoded = decode(whole, 1);
	require(decoded.directory_update == directory, "directory round trip");
	require(decoded.frames.size() == 3 && decoded.frames[0].name == "empty" && decoded.frames[1].name == "pay" &&
				decoded.frames[2].name == "zed",
			"three frames in name order");
	// An empty namespace with no store: a full copy of nothing at position 0.
	require(decoded.frames[0].kind == d::FrameKind::FullCopy && decoded.frames[0].pieces.size() == 1 &&
				decoded.frames[0].pieces[0].records.empty() && decoded.frames[0].to.position == 0,
			"an empty namespace's copy");
	// A store of the namespace's incarnation at 0 (subscribed from the directory) gets entries.
	require(decoded.frames[2].kind == d::FrameKind::Entries && decoded.frames[2].after == 0,
			"a directory subscription starts from position 0");
	require(apply_frame(pay.state_at(12), decoded.frames[1]) == pay.state_at(40), "pay rebuilt");
}

// A writer for hand-built bodies the decoder must refuse.
struct Body {
	std::string out;
	Body &array(std::uint32_t n) { return m::array(out, n), *this; }
	Body &u(std::uint64_t v) { return m::unsigned_integer(out, v), *this; }
	Body &s(std::string_view v) { return m::string(out, v), *this; }
	Body &id(char c) {
		auto value = history_fixture::id(c);
		return m::string(out, {value.data(), value.size()}, true), *this;
	}
	Body &nil() { return out.push_back('\xc0'), *this; }
	Body &def(std::int64_t v) { return array(4).u(static_cast<std::uint64_t>(v)).nil().nil().array(0); }
	Body &frame(std::string_view name, unsigned kind, std::uint64_t after, std::uint64_t to, std::uint64_t pieces,
				char cluster = 1) {
		return array(5).s(name).u(kind).u(after).array(3).id(cluster).id(2).u(to).u(pieces);
	}
	Body &piece(unsigned kind, std::uint64_t first, std::uint64_t last) { return array(3).u(kind).u(first).u(last); }
	Body &record(std::string_view key, std::uint64_t revision, std::optional<std::int64_t> value) {
		array(3).s(key).u(revision);
		return value ? def(*value) : nil();
	}
};
Body head(std::uint64_t frames) { return Body{}.array(3).u(1).nil().u(frames); }

void refusals() {
	auto good = head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out;
	require(!rejected(good), "the hand-built entries frame decodes");
	require(!rejected(head(1)
						  .frame("a", 2, 1, 3, 2)
						  .piece(3, 1, 2)
						  .record("a:x", 2, 1)
						  .nil()
						  .piece(1, 3, 3)
						  .record("a:x", 3, 1)
						  .nil()
						  .out),
			"the hand-built blocks frame decodes");
	constexpr auto max = std::numeric_limits<std::uint64_t>::max();
	require(!rejected(head(1).frame("a", 2, max - 2, max, 1).piece(3, max - 1, max).record("a:x", max, 1).nil().out),
			"a span reaching 2^64-1 decodes");
	auto checks = std::vector<std::pair<std::string, std::string>>{
		{"trailing byte", good + "\xc0"},
		{"truncated", good.substr(0, good.size() - 1)},
		{"version 2", Body{}.array(3).u(2).nil().u(0).out},
		{"entry skips a position", head(1).frame("a", 1, 1, 3, 1).piece(1, 3, 3).record("a:x", 3, 1).nil().out},
		{"entry piece count short", head(1).frame("a", 1, 1, 3, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"entry without records", head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).nil().out},
		{"entry revision not its position", head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:x", 1, 1).nil().out},
		{"key of another namespace", head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("b:x", 2, 1).nil().out},
		{"keys out of order", head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:y", 2, 1).record("a:x", 2, 1).nil().out},
		{"duplicate key", head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).record("a:x", 2, 1).nil().out},
		{"span record at or below R", head(1).frame("a", 2, 3, 6, 1).piece(3, 1, 6).record("a:x", 3, 1).nil().out},
		{"span not holding R + 1", head(1).frame("a", 2, 3, 9, 1).piece(3, 5, 9).record("a:x", 6, 1).nil().out},
		{"whole block not at R + 1", head(1).frame("a", 2, 3, 6, 1).piece(2, 1, 6).record("a:x", 5, 1).nil().out},
		{"spans not contiguous",
		 head(1).frame("a", 2, 3, 9, 2).piece(3, 1, 5).record("a:x", 5, 1).nil().piece(2, 7, 9).record("a:x", 9, 1).nil().out},
		{"piece after the target at 2^64-1",
		 head(1)
			 .frame("a", 2, max - 2, max, 2)
			 .piece(3, max - 1, max)
			 .record("a:x", max, 1)
			 .nil()
			 .piece(3, 0, max)
			 .record("a:x", max - 1, 1)
			 .nil()
			 .out},
		{"blocks frame starting with an entry", head(1).frame("a", 2, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"span after an entry", head(1)
									.frame("a", 2, 1, 4, 3)
									.piece(3, 1, 2)
									.record("a:x", 2, 1)
									.nil()
									.piece(1, 3, 3)
									.record("a:x", 3, 1)
									.nil()
									.piece(3, 4, 4)
									.record("a:x", 4, 1)
									.nil()
									.out},
		{"tombstone in a full copy", head(1).frame("a", 3, 0, 2, 1).piece(4, 0, 2).record("a:x", 2, {}).nil().out},
		{"full copy record above its position", head(1).frame("a", 3, 0, 2, 1).piece(4, 0, 1).record("a:x", 2, 1).nil().out},
		{"full copy with an after", head(1).frame("a", 3, 1, 2, 1).piece(4, 0, 2).record("a:x", 2, 1).nil().out},
		{"frame at or below its after", head(1).frame("a", 1, 2, 2, 1).piece(1, 3, 3).record("a:x", 3, 1).nil().out},
		{"frames out of order",
		 head(2).frame("b", 1, 1, 2, 1).piece(1, 2, 2).record("b:x", 2, 1).nil().frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"frame count short", head(2).frame("a", 1, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"zero cluster", head(1).frame("a", 1, 1, 2, 1, 0).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"unknown frame kind", head(1).frame("a", 4, 1, 2, 1).piece(1, 2, 2).record("a:x", 2, 1).nil().out},
		{"unknown piece kind", head(1).frame("a", 1, 1, 2, 1).piece(5, 2, 2).record("a:x", 2, 1).nil().out},
		{"invalid name", head(1).frame("a b", 1, 1, 2, 1).piece(1, 2, 2).record("a b:x", 2, 1).nil().out},
	};
	for (const auto &[what, body] : checks)
		require(rejected(body), "refuses: " + what);
	// A percentage over 100 and override keys out of order.
	auto bad_percent = head(1).frame("a", 1, 1, 2, 1).piece(1, 2, 2).array(3).s("a:x").u(2).array(4).u(0).u(101).u(1).array(0).nil().out;
	require(rejected(bad_percent), "refuses a rollout over 100");
	auto unordered = head(1)
						 .frame("a", 1, 1, 2, 1)
						 .piece(1, 2, 2)
						 .array(3)
						 .s("a:x")
						 .u(2)
						 .array(4)
						 .u(0)
						 .nil()
						 .nil()
						 .array(2)
						 .array(3)
						 .s("g2")
						 .u(0)
						 .u(1)
						 .array(3)
						 .s("g1")
						 .u(0)
						 .u(1)
						 .nil()
						 .out;
	require(rejected(unordered), "refuses override keys out of order");
}

// Histories that break their contract fail the source, never the client.
void source_failures() {
	const auto txs = churn();
	auto fails = [](const d::NamespaceHistory &history, std::uint64_t r) {
		d::NamespaceRequest request{"pay", &history, d::NamespaceCursor{id(1), id(2), r}};
		try {
			d::encode_session_frames({}, std::span(&request, 1));
		} catch (const d::HistoryFailure &) {
			return true;
		}
		return false;
	};
	require(fails(TestHistory(txs, 8, {{9, 16, true}, {18, 30, true}}, 30), 35), "spans with a gap");
	require(fails(TestHistory(txs, 8, {{9, 16, false}, {17, 30, true}}, 30), 35), "a block after a level-0 section");
	require(fails(TestHistory(txs, 8, {{9, 16, true}}, 30), 35), "spans short of the ring");
	require(fails(TestHistory(txs, 31, {}, 30), 35), "cut above the ring");
	struct SkippingRing : TestHistory {
		using TestHistory::TestHistory;
		std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
			auto out = rows(span(after + 1, after + 2));
			for (auto &[key, held] : rows(span(after + 4, transactions_.size())))
				out.emplace_back(key, held);
			return std::make_unique<VectorReader>(out);
		}
	};
	require(fails(SkippingRing(txs, 8, {{9, 30, true}}, 30), 30), "a ring that skips a position");
	struct WideSpan : TestHistory {
		using TestHistory::TestHistory;
		std::unique_ptr<d::HistoryReader> read_span(std::size_t) const override {
			return std::make_unique<VectorReader>(rows(span(1, 8)));
		}
	};
	require(fails(WideSpan(txs, 8, {{9, 30, true}}, 30), 10), "a span record outside the span");
	struct CopyWithTombstone : TestHistory {
		using TestHistory::TestHistory;
		std::unique_ptr<d::HistoryReader> read_full_copy() const override {
			return std::make_unique<VectorReader>(rows(span(1, ring_start_)));
		}
	};
	require(fails(CopyWithTombstone(txs, 8, {{9, 30, true}}, 30), 0), "a full copy holding a tombstone");
}
} // namespace

int main() {
	try {
		golden();
		tiers();
		other_incarnation();
		chunking_and_directory();
		refusals();
		source_failures();
	} catch (const std::exception &error) {
		std::cerr << "test_distribution_session_frames: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
