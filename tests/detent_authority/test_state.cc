#include "state.hh"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <random>

using namespace detent;
using namespace detent::authority;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
Command open(std::uint64_t expected = 0, char nonce = 'a') {
	Command c;
	c.context = context();
	c.kind = Kind::Open;
	c.producer = "writer";
	c.generation = expected;
	c.nonce[0] = nonce;
	return c;
}
Command transaction(std::uint64_t sequence, std::vector<Mutation> operations, std::uint64_t generation = 1) {
	Command c;
	c.context = context();
	c.producer = "writer";
	c.generation = generation;
	c.sequence = sequence;
	c.name_space = "ns";
	c.mutations = std::move(operations);
	return c;
}
template <class F> bool throws(F &&f) {
	try {
		f();
	} catch (const std::exception &) {
		return true;
	}
	return false;
}
void trees() {
	detent::authority::detail::PersistentMap<int, int> tree;
	std::map<int, int> expected;
	std::mt19937 random(37);
	for (int turn = 0; turn < 20000; ++turn) {
		auto key = static_cast<int>(random() % 4096);
		auto before = tree;
		if (random() & 1) {
			tree = tree.set(key, turn);
			expected[key] = turn;
		} else {
			tree = tree.erase(key);
			expected.erase(key);
		}
		assert(tree.size() == expected.size());
		assert(tree.height() < 24);
		auto found = expected.find(key);
		auto actual = tree.find(key);
		assert((found == expected.end()) == !actual);
		if (actual) {
			assert(*actual == found->second);
		}
		tree.collect(128);
		if (turn % 100 == 0) {
			auto cursor = expected.begin();
			tree.for_each([&](int k, int v) {
				assert(cursor != expected.end() && cursor->first == k && cursor->second == v);
				++cursor;
			});
			assert(cursor == expected.end());
		}
		if (auto old = before.find(key)) {
			auto value = *old;
			auto another = tree.set(key, -1);
			assert(*before.find(key) == value && *another.find(key) == -1);
		}
	}
}
void semantics() {
	Limits limits;
	limits.results = 3;
	limits.keys = 2;
	State state(context(), limits);
	std::uint64_t index = 0;
	auto apply = [&](const Command &c) {
		auto encoded = encode(c, limits);
		auto decoded = decode(encoded, limits);
		assert(encode(decoded, limits) == encoded);
		return state.apply(++index, decoded);
	};
	assert(apply(open()).generation == 1);
	assert(apply(open()).generation == 1);
	auto first = transaction(1, {{"ns:a", IntDef{1}, 0, 0}, {"ns:b", BoolDef{}, 0, 0}});
	auto result = apply(first);
	assert(result.status == Status::Applied && result.revision == 1 && state.size() == 2);
	auto capture = state;
	auto reordered = first;
	std::reverse(reordered.mutations.begin(), reordered.mutations.end());
	assert(digest(reordered, limits) == digest(first, limits));
	assert(apply(reordered) == result && state.revision() == 1);
	auto conflict_identity = first;
	conflict_identity.mutations[0].value = IntDef{9};
	assert(apply(conflict_identity).status == Status::IdentityConflict);
	auto conflict = transaction(2, {{"ns:a", IntDef{3}, 99, {}}, {"ns:b", std::nullopt, 1, {}}});
	assert(apply(conflict).status == Status::Conflict && state.size() == 2 && state.revision() == 1);
	assert(apply(transaction(3, {{"ns:c", IntDef{3}, {}, {}}})).status == Status::Capacity &&
		   !state.find("ns:c"));
	assert(apply(transaction(4, {{"ns:a", IntDef{1}, {}, {}}})).status == Status::NoChange &&
		   state.revision() == 1);
	assert(apply(first).status == Status::ResultUnavailable && state.revision() == 1);
	assert(apply(transaction(6, {{"ns:a", IntDef{6}, {}, {}}})).status == Status::SequenceGap);
	auto replacement = transaction(5, {{"ns:a", std::nullopt, 1, {}}, {"ns:c", IntDef{8}, 0, 1}});
	assert(apply(replacement).status == Status::Applied && state.size() == 2 && !state.find("ns:a"));
	assert(capture.find("ns:a") && capture.size() == 2 && capture.revision() == 1);
	auto absent_old = transaction(6, {{"ns:a", IntDef{1}, 0, 1}});
	assert(apply(absent_old).status == Status::Conflict);
	auto recreate = transaction(7, {{"ns:c", std::nullopt, 2, {}}, {"ns:a", IntDef{1}, 0, 2}});
	assert(apply(recreate).status == Status::Applied && state.find("ns:a")->revision == 3);
	assert(apply(transaction(8, {{"ns:a", std::nullopt, 1, {}}})).status == Status::Conflict);
	auto bytes = state.checkpoint();
	auto recovered = State::restore(bytes, context(), limits);
	assert(recovered.checkpoint() == bytes &&
		   recovered.lookup("writer", 1, 7) == state.lookup("writer", 1, 7));
	for (std::size_t n = 0; n < bytes.size(); ++n) {
		assert(throws([&] { State::restore(std::string_view(bytes).substr(0, n), context(), limits); }));
	}
	Command close = open();
	close.kind = Kind::Close;
	close.generation = 1;
	close.nonce = {};
	assert(apply(close).status == Status::Applied);
	assert(apply(first).status == Status::FencedSession);
	assert(apply(open()).status == Status::FencedSession);
	assert(apply(open(1, 'b')).generation == 2);
	assert(apply(open()).status == Status::FencedSession);
	assert(apply(first).status == Status::FencedSession);
	auto reset = transaction(1, {{"ns:a", IntDef{99}, {}, {}}}, 2);
	assert(apply(reset).status == Status::Applied);
	auto foreign = context();
	foreign.epoch[0] = 'x';
	assert(throws([&] { State::restore(bytes, foreign, limits); }));
	auto wrong_limits = limits;
	++wrong_limits.results;
	assert(throws([&] { State::restore(bytes, context(), wrong_limits); }));
	auto grouped = std::vector<Command>{transaction(2, {{"ns:a", IntDef{100}, {}, {}}}, 2),
										transaction(3, {{"ns:a", IntDef{101}, {}, {}}}, 2)};
	auto batch = encode_batch(grouped, limits);
	auto mismatched = limits;
	++mismatched.results;
	assert(throws([&] { decode_batch(batch, mismatched); }));
	auto outcomes = state.apply_batch(++index, decode_batch(batch, limits));
	assert(outcomes.size() == 2 && outcomes[0].revision + 1 == outcomes[1].revision &&
		   state.applied_index() == index);
	for (std::size_t n = 0; n < batch.size(); ++n) {
		assert(throws([&] { decode_batch(std::string_view(batch).substr(0, n), limits); }));
	}
	auto malformed = first;
	malformed.mutations.push_back(malformed.mutations.front());
	assert(throws([&] { encode(malformed, limits); }));
	malformed = first;
	malformed.mutations[0].absence_revision.reset();
	assert(throws([&] { encode(malformed, limits); }));
	malformed = first;
	malformed.mutations[0].key = "other:key";
	assert(throws([&] { encode(malformed, limits); }));
	BoolDef opaque;
	opaque.overrides[std::string("a\0b", 3)] = GroupRollout{17};
	auto opaque_command = transaction(4, {{"ns:a", opaque, {}, {}}}, 2);
	auto decoded = decode(encode(opaque_command, limits), limits);
	assert(decoded.mutations[0].value == opaque_command.mutations[0].value);
	assert(state.apply(++index, opaque_command).status == Status::Applied);
	assert(*State::restore(state.checkpoint(), context(), limits).find("ns:a")->value == KnobDef{opaque});
	assert(sizeof(Receipt) <= 128);
}
void incremental_batches() {
	Limits limits;
	limits.keys = 2;
	State original(context(), limits);
	const auto before = original.checkpoint();
	std::vector<Command> commands{open(),
								  transaction(1, {{"ns:a", IntDef{1}, 0, 0}}),
								  transaction(1, {{"ns:a", IntDef{1}, 0, 0}}),
								  transaction(2, {{"ns:a", IntDef{2}, 99, {}}}),
								  transaction(3, {{"ns:b", IntDef{3}, 0, 1}}),
								  transaction(4, {{"ns:c", IntDef{4}, 0, 2}})};
	auto expected = original;
	auto results = expected.apply_batch(1, commands, 2);
	assert(results[3].status == Status::Conflict && results[5].status == Status::Capacity);
	auto bytes = encode_batch(commands, limits);
	std::size_t exact_size = batch_framing_bytes(limits);
	for (const auto &command : commands) exact_size += 8 + encode(command, limits).size();
	assert(bytes.size() == exact_size && batch_framing_bytes(limits) == 96);
	assert(encode_batch(commands, limits, exact_size) == bytes);
	assert(throws([&] { encode_batch(commands, limits, exact_size - 1); }));
	BatchDecoder decoder(bytes, limits);
	BatchApplication candidate(original, 1, 2, decoder.count());
	while (!decoder.done()) {
		auto work = decoder.next_work();
		assert(work.bytes > 8 && work.mutations <= 1);
		assert(throws([&] { candidate.finish(); }));
		candidate.advance(decoder.next());
		assert(original.checkpoint() == before);
	}
	BatchApplication moved(std::move(candidate));
	assert(throws([&] { candidate.finish(); }));
	auto outcome = moved.finish();
	assert(outcome.results == results && outcome.state.checkpoint() == expected.checkpoint());
	assert(throws([&] { moved.finish(); }));
	for (const auto &invalid : {bytes.substr(0, bytes.size() - 1), bytes + "trailing"}) {
		BatchDecoder broken(invalid, limits);
		BatchApplication unpublished(original, 1, 2, broken.count());
		assert(throws([&] {
			while (!broken.done())
				unpublished.advance(broken.next());
		}));
		assert(!broken.done() && !unpublished.done());
		assert(throws([&] { broken.next(); }));
		assert(original.checkpoint() == before);
	}
	BatchApplication poisoned(original, 1, 2, 2);
	poisoned.advance(open());
	auto bad = commands[1];
	bad.mutations.push_back(bad.mutations.front());
	assert(throws([&] { poisoned.advance(bad); }));
	assert(throws([&] { poisoned.advance(commands[1]); }));
	assert(throws([&] { poisoned.finish(); }));
	assert(original.checkpoint() == before);
}
void streaming_images() {
	State state(context());
	std::uint64_t index = 1, sequence = 0;
	state.apply(index, open());
	for (int batch = 0; batch < 40; ++batch) {
		std::vector<Mutation> mutations;
		for (int key = 0; key < 128; ++key)
			mutations.push_back({"ns:key" + std::to_string(batch * 128 + key), IntDef{key}, {}, {}});
		assert(state.apply(++index, transaction(++sequence, std::move(mutations))).status == Status::Applied);
	}
	auto bytes = state.checkpoint();
	for (std::size_t fragment : {std::size_t{1}, std::size_t{73}, std::size_t{65536}}) {
		ImageDecoder decoder(context());
		std::size_t offset = 0;
		while (!decoder.finished()) {
			if (decoder.needs_input()) {
				auto count = std::min(fragment, bytes.size() - offset);
				decoder.feed(std::string_view(bytes).substr(offset, count), offset + count == bytes.size());
				offset += count;
			}
			decoder.step(1);
		}
		auto recovered = decoder.take();
		assert(recovered.checkpoint() == bytes);
		assert(recovered.retained_nodes() < recovered.size() + 1024);
	}
	assert(throws([&] { State::restore(bytes + "trailing", context()); }));
	ImageDecoder invalid(context());
	std::size_t offset = 0;
	assert(throws([&] {
		while (!invalid.finished()) {
			if (invalid.needs_input()) {
				auto count = std::min<std::size_t>(65536, bytes.size() - 1 - offset);
				invalid.feed(std::string_view(bytes).substr(offset, count),
							 offset + count == bytes.size() - 1);
				offset += count;
			}
			invalid.step();
		}
	}));
	auto retired = invalid.discard();
	assert(retired.retained_nodes() >= state.size());
	while (retired.reclamation_pending()) {
		auto before = retired.retained_nodes();
		retired.collect(1);
		assert(before - retired.retained_nodes() <= 3);
	}
	assert(retired.retained_nodes() == 0);

	// The image header stores revision after the context, limits, index and term.
	auto exhausted = bytes;
	for (std::size_t i = 152; i < 160; ++i)
		exhausted[i] = static_cast<char>(255);
	auto recovered = State::restore(exhausted, context());
	auto outcome = recovered.apply(++index, transaction(++sequence, {{"ns:key0", IntDef{-1}, {}, {}}}));
	assert(outcome.status == Status::Exhausted);
	assert(recovered.find("ns:key0")->revision == state.find("ns:key0")->revision);
	assert(recovered.lookup("writer", 1, sequence) == outcome);
	assert(State::restore(recovered.checkpoint(), context()).lookup("writer", 1, sequence) == outcome);
}

void registry_and_generation_limits() {
	Limits limits;
	limits.producers = 1;
	limits.namespaces = 1;
	State state(context(), limits);
	assert(state.apply(1, open()).status == Status::Applied);
	auto other = open();
	other.producer = "other";
	assert(state.apply(2, other).status == Status::Capacity);
	assert(state.apply(3, transaction(1, {{"ns:key", IntDef{1}, {}, {}}})).status == Status::Applied);
	auto next = transaction(2, {{"other:key", IntDef{2}, {}, {}}});
	next.name_space = "other";
	assert(state.apply(4, next).status == Status::Capacity);
	assert(state.apply(5, transaction(3, {{"ns:key", std::nullopt, {}, {}}})).status == Status::Applied);
	next.sequence = 4;
	assert(state.apply(6, next).status == Status::Capacity); // Empty namespace retains its slot.
	State session(context(), limits);
	session.apply(1, open());
	auto image = session.checkpoint();
	auto position = image.find("writer");
	assert(position != std::string::npos);
	auto generation = position + 6;
	for (std::size_t i = 0; i < 8; ++i)
		image[generation + i] = static_cast<char>(255);
	auto restored = State::restore(image, context(), limits);
	auto rotate = open(std::numeric_limits<std::uint64_t>::max(), 'b');
	assert(restored.apply(2, rotate).status == Status::Exhausted);
	assert(restored.producer("writer")->generation == std::numeric_limits<std::uint64_t>::max());
	image = session.checkpoint();
	image[152] = 1; // Nonzero revision with an empty root is legal.
	auto decoder = ImageDecoder(context(), limits);
	decoder.feed(image, true);
	decoder.step(1);
	auto retired = decoder.discard();
	while (retired.reclamation_pending())
		retired.collect(1);
}

void concurrent_cas() {
	State state(context());
	auto first = open();
	auto second = open();
	second.producer = "second";
	state.apply(1, first);
	state.apply(2, second);
	auto a = transaction(1, {{"ns:a", IntDef{1}, {}, {}}, {"ns:b", IntDef{2}, {}, {}}});
	assert(state.apply(3, a).revision == 1);
	auto b = transaction(1, {{"ns:a", IntDef{1}, 1, {}}, {"ns:b", IntDef{3}, 1, {}}});
	b.producer = "second";
	assert(state.apply(4, b).revision == 2);
	assert(state.find("ns:a")->revision == 1 && state.find("ns:b")->revision == 2);
	auto stale = transaction(2, {{"ns:a", IntDef{4}, 1, {}}, {"ns:b", IntDef{5}, 1, {}}});
	assert(state.apply(5, stale).status == Status::Conflict);
	assert(state.find("ns:a")->revision == 1 && state.revision() == 2);
	auto before = state.checkpoint();
	assert(throws([&] { state.apply(std::numeric_limits<std::uint64_t>::max(), a); }));
	assert(state.checkpoint() == before);
}

} // namespace
int main() {
	trees();
	incremental_batches();
	streaming_images();
	semantics();
	std::cout << "authority state histories passed\n";
}
