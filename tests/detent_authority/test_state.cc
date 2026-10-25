#include "deferred_values.hh"
#include "prepared.hh"
#include "state.hh"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sha256.h>
#include <thread>
#include <tuple>

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
void bounded_applications() {
	State legacy(context()), bounded(context());
	std::uint64_t index = 0;
	auto apply = [&](const Command &command) {
		auto before = bounded.checkpoint();
		auto bytes = encode(command);
		CommandDecoder decoder(bytes);
		while (!decoder.done()) {
			CommandWorkBudget b;
			decoder.step(b);
		}
		CommandApplication application(bounded, ++index, 1, decoder.take());
		unsigned turns = 0;
		while (!application.done()) {
			CommandWorkBudget budget;
			application.step(budget);
			assert(budget.bytes <= 8192 && budget.records <= 16 && budget.updates <= 8 &&
				   budget.receipts <= 32);
			assert(bounded.checkpoint() == before);
			assert(++turns < 10000);
		}
		auto result = application.finish();
		auto expected = legacy.apply(index, command);
		assert(result.results.size() == 1 && result.results.front() == expected);
		bounded = std::move(result.state);
		assert(bounded.checkpoint() == legacy.checkpoint());
		while (bounded.reclamation_pending()) {
			CommandWorkBudget b;
			bounded.collect(b);
		}
		return turns;
	};
	apply(open());
	BoolDef large;
	large.rollout_pct = 45;
	for (unsigned i = 0; i < 256; ++i)
		large.overrides.emplace("group" + std::to_string(i), GroupRollout{33});
	auto first = transaction(1, {{"ns:a", large, {}, {}}});
	apply(first);
	assert(apply(transaction(2, {{"ns:a", large, {}, {}}})) > 16);
	apply(first); // Retained duplicate returns its original result.
	auto mismatch = first;
	mismatch.mutations[0].expected = 1;
	apply(mismatch);
	apply(transaction(4, {{"ns:a", IntDef{9}, {}, {}}})); // Sequence gap.
	apply(transaction(3, {{"ns:a", IntDef{9}, 99, {}}})); // Conflict consumes sequence.
	for (unsigned i = 4; i <= 265; ++i)
		apply(transaction(i, {{"ns:n", IntDef{i}, {}, {}}}));
	apply(first); // Original receipt has been evicted.
	auto close = open();
	close.kind = Kind::Close;
	close.generation = 1;
	close.nonce = {};
	assert(apply(close) > 8);								// Full receipt window must yield while copying.
	apply(transaction(266, {{"ns:n", IntDef{1}, {}, {}}})); // Inactive session.
	apply(open(1, 'b'));
	apply(transaction(1, {{"ns:a", {}, {}, {}}, {"ns:n", IntDef{2}, {}, {}}}, 2));
	auto unknown = transaction(1, {{"ns:z", large, {}, {}}});
	unknown.producer = "unknown";
	assert(apply(unknown) > 8); // Rejected definitions are disposed incrementally too.
	// Abandon a candidate after it starts writing: the public source stays exact.
	auto command = transaction(2, {{"ns:z", large, {}, {}}}, 2);
	auto bytes = encode(command);
	CommandDecoder decoder(bytes);
	while (!decoder.done()) {
		CommandWorkBudget b;
		decoder.step(b);
	}
	auto before = bounded.checkpoint();
	CommandApplication cancelled(bounded, index + 1, 1, decoder.take());
	CommandWorkBudget one;
	one.updates = 1;
	cancelled.step(one);
	unsigned turns = 0;
	for (;;) {
		CommandWorkBudget b;
		b.receipts = 1;
		bool drained = cancelled.discard(b);
		assert(bounded.checkpoint() == before);
		assert(++turns < 10000);
		if (drained)
			break;
	}
	assert(turns > 256);
	CommandDecoder reusable(bytes);
	while (!reusable.done()) {
		CommandWorkBudget budget;
		reusable.step(budget);
	}
	auto capability = reusable.take();
	auto owner = std::move(capability);
	assert(throws([&] { CommandApplication invalid(bounded, index + 1, 1, std::move(capability)); }));
	assert(bounded.checkpoint() == before);
	CommandApplication valid(bounded, index + 1, 1, std::move(owner));
	while (!valid.done()) {
		CommandWorkBudget budget;
		valid.step(budget);
	}
	assert(valid.finish().results.front().status == Status::Applied);
	auto foreign = command;
	foreign.context.epoch[0] = 'f';
	auto foreign_bytes = encode(foreign);
	CommandDecoder foreign_decoder(foreign_bytes, {}, context());
	CommandWorkBudget budget;
	assert(throws([&] { foreign_decoder.step(budget); }));
	// Context rejection happens before override rows are decoded.
	budget = {};
	budget.receipts = 1;
	assert(foreign_decoder.discard(budget));
}

void deferred_values() {
	struct Value {
		std::deque<std::string> rows;
		unsigned *destroyed = nullptr;
		~Value() {
			if (destroyed)
				++*destroyed;
		}
	};
	using Arena = detent::authority::detail::DeferredValues<Value>;
	unsigned destroyed = 0;
	auto arena = std::make_shared<Arena>();
	auto value = arena->create(1024);
	value->destroyed = &destroyed;
	for (unsigned i = 0; i < 256; ++i)
		value->rows.emplace_back(128, 'x');
	std::weak_ptr<Value> observer = value;
	auto alias = value;
	value.reset();
	assert(!observer.expired() && arena->count() == 0);
	std::thread releaser([alias = std::move(alias)]() mutable { alias.reset(); });
	releaser.join();
	assert(observer.expired() && !observer.lock());
	assert(arena->count() == 1 && arena->encoded_bytes() == 1024 && destroyed == 0);
	for (unsigned i = 0; i < 256; ++i) {
		assert(arena->collect_one([](Value &dead) {
			dead.rows.pop_front();
			return false;
		}) == 0);
		assert(arena->count() == 1 && arena->encoded_bytes() == 1024);
	}
	assert(arena->collect_one([](Value &dead) { return dead.rows.empty(); }) == 1);
	assert(!arena->pending() && arena->count() == 0 && arena->encoded_bytes() == 0 && destroyed == 1);
	observer.reset();
	// Last-arena destruction must drain both its partially consumed owner
	// list and newer incoming originals. No client can lock either value.
	auto first = arena->create(17);
	first->destroyed = &destroyed;
	first->rows.emplace_back("partial");
	first.reset();
	assert(arena->collect_one([](Value &) { return false; }) == 0);
	auto second = arena->create(23);
	second->destroyed = &destroyed;
	second.reset();
	assert(arena->count() == 2 && arena->encoded_bytes() == 40);
	arena.reset();
	assert(destroyed == 3);
}
void prepared_proposals() {
	auto encoded = [](const Command &command, Limits limits = {}) {
		CommandEncoder encoder(command, limits);
		while (!encoder.done()) {
			CommandWorkBudget b;
			encoder.step(b);
		}
		return encoder.take_encoded();
	};
	auto finish = [](ProposalBuilder &builder) {
		builder.seal();
		unsigned turns = 0;
		while (!builder.done()) {
			CommandWorkBudget b;
			builder.step(b);
			assert(b.bytes <= 8192 && b.records <= 16 && b.updates == 8 && b.receipts == 32);
			assert(++turns < 10000);
		}
		return builder.take();
	};
	auto append = [](ProposalBuilder &builder, EncodedCommand &command) {
		bool accepted = builder.append(command);
		while (builder.command_pending()) {
			CommandWorkBudget b;
			builder.step(b);
		}
		return accepted;
	};
	std::vector<Command> commands{open(),
								  transaction(1, {{"ns:z", BoolDef{}, {}, {}}, {"ns:a", IntDef{-99}, 0, 0}})};
	auto expected = encode_batch(commands);
	ProposalBuilder builder(context(), {}, expected.size());
	for (const auto &c : commands) {
		auto command = encoded(c);
		assert(append(builder, command));
		assert(throws([&] { builder.append(command); })); // Accepted append consumed its capability.
	}
	auto prepared = finish(builder);
	assert(prepared.bytes() == expected && prepared.valid());
	SHA256 independent;
	independent.add(expected.data(), expected.size());
	Digest hash{};
	independent.getHash(hash.data());
	assert(prepared.digest() == hash);
	auto moved = std::move(prepared);
	assert(!prepared.valid() && moved.valid());
	assert(throws([&] { builder.take(); }));
	ProposalBuilder short_batch(context(), {}, expected.size() - 1);
	auto first = encoded(commands.front());
	assert(append(short_batch, first));
	auto carry = encoded(commands.back());
	const auto size = carry.size();
	for (unsigned retry = 0; retry < 4; ++retry) {
		assert(!short_batch.append(carry));
		assert(carry.size() == size && short_batch.count() == 1);
	}
	assert(finish(short_batch).bytes() == encode_batch({commands.front()}));
	ProposalBuilder next(context(), {}, batch_framing_bytes() + 8 + size);
	assert(append(next, carry));
	assert(finish(next).bytes() == encode_batch({commands.back()}));
	ProposalBuilder maximum(context());
	for (unsigned i = 0; i < 64; ++i) {
		auto command = encoded(commands.front());
		assert(append(maximum, command));
	}
	auto sixty_fifth = encoded(commands.front());
	assert(!maximum.append(sixty_fifth));
	auto full = finish(maximum);
	assert(decode_batch(full.bytes()).size() == 64);
	ProposalBuilder mismatch(context());
	auto foreign = commands.front();
	foreign.context.epoch[0] = 'f';
	auto foreign_encoded = encoded(foreign);
	assert(throws([&] { mismatch.append(foreign_encoded); }));
	assert(mismatch.count() == 0);
	Limits other;
	other.mutations = 127;
	auto changed_profile = encoded(commands.front(), other);
	assert(throws([&] { mismatch.append(changed_profile); }));
	auto good = encoded(commands.front());
	assert(append(mismatch, good));
	mismatch.discard();
	assert(!mismatch.done());
}

void command_encoding() {
	auto encode_in_turns = [](const Command &command, Limits limits = {}) {
		CommandEncoder encoder(command, limits);
		unsigned turns = 0;
		while (!encoder.done()) {
			CommandWorkBudget budget;
			encoder.step(budget);
			assert(budget.bytes <= 8192 && budget.records <= 16 && budget.updates == 8 &&
				   budget.receipts <= 32);
			assert(++turns < 10000);
		}
		auto bytes = encoder.take();
		assert(!encoder.done() && throws([&] { encoder.take(); }));
		return bytes;
	};
	auto close = open();
	close.kind = Kind::Close;
	close.generation = 1;
	close.nonce = {};
	assert(encode_in_turns(open()) == encode(open()));
	assert(encode_in_turns(close) == encode(close));
	BoolDef definition;
	definition.rollout_pct = 51;
	definition.overrides.emplace(std::string("\0group", 6), true);
	definition.overrides.emplace("unicode-\xc3\xa9", GroupRollout{42});
	for (unsigned i = 0; i < 254; ++i)
		definition.overrides.emplace("group" + std::to_string(i), GroupRollout{35});
	auto command =
		transaction(1, {{"ns:z", definition, {}, {}}, {"ns:b", IntDef{-99}, {}, {}}, {"ns:a", {}, 0, 0}});
	auto canonical = encode(command);
	assert(encode_in_turns(command) == canonical);
	std::reverse(command.mutations.begin(), command.mutations.end());
	assert(encode_in_turns(command) == canonical);
	std::vector<Command> invalid;
	auto add = [&](auto change) {
		invalid.push_back(command);
		change(invalid.back());
	};
	add([](auto &c) { c.context.epoch = {}; });
	add([](auto &c) { c.kind = static_cast<Kind>(99); });
	add([](auto &c) { c.producer = std::string(129, 'x'); });
	add([](auto &c) { c.producer = "\xff"; });
	add([](auto &c) { c.generation = 0; });
	add([](auto &c) { c.sequence = 0; });
	add([](auto &c) { c.name_space = "other"; });
	add([](auto &c) { c.mutations.push_back(c.mutations.front()); });
	add([](auto &c) { c.mutations.front().absence_revision = {}; });
	add([](auto &c) { std::get<BoolDef>(*c.mutations.back().value).rollout_pct = 101; });
	add([](auto &c) { std::get<BoolDef>(*c.mutations.back().value).overrides.emplace("extra", true); });
	add([](auto &c) {
		auto &d = std::get<BoolDef>(*c.mutations.back().value);
		d.overrides.erase(d.overrides.begin());
		d.overrides.emplace(std::string(129, 'x'), true);
	});
	add([](auto &c) {
		auto &d = std::get<BoolDef>(*c.mutations.back().value);
		d.overrides.begin()->second = GroupRollout{101};
	});
	for (const auto &bad : invalid) {
		assert(throws([&] { encode(bad); }));
		CommandEncoder encoder(bad);
		assert(throws([&] {
			while (!encoder.done()) {
				CommandWorkBudget b;
				encoder.step(b);
			}
		}));
		assert(throws([&] {
			CommandWorkBudget b;
			encoder.step(b);
		}));
		unsigned turns = 0;
		for (;;) {
			CommandWorkBudget b;
			b.receipts = 1;
			if (encoder.discard(b))
				break;
			assert(++turns < 4096);
		}
	}
	std::vector<Mutation> operations;
	for (unsigned i = 0; i < 128; ++i)
		operations.push_back({"ns:key" + std::to_string(i), IntDef{1}, {}, {}});
	auto many = transaction(1, std::move(operations));
	CommandEncoder abandoned(many);
	CommandWorkBudget budget;
	abandoned.step(budget);
	unsigned turns = 0;
	for (;;) {
		CommandWorkBudget b;
		b.receipts = 1;
		if (abandoned.discard(b))
			break;
		assert(++turns < 4096);
	}
	assert(turns > 1);
	assert(encode_in_turns(many) == encode(many));
}

void command_continuations() {
	auto drive = [](CommandDecoder &decoder) {
		std::size_t turns = 0;
		while (!decoder.done()) {
			CommandWorkBudget budget;
			decoder.step(budget);
			assert(budget.bytes <= 8192 && budget.records <= 16 && budget.updates == 8 &&
				   budget.receipts == 32);
			assert(++turns < 10000);
		}
		return turns;
	};
	BoolDef definition;
	definition.rollout_pct = 17;
	definition.overrides[std::string("\0group", 6)] = true;
	definition.overrides["unicode-\xc3\xa9"] = GroupRollout{42};
	for (unsigned i = 0; i < 254; ++i)
		definition.overrides["g" + std::to_string(i)] = GroupRollout{25};
	Limits limits;
	auto command =
		transaction(1, {{"ns:a", definition, 0, 0}, {"ns:b", IntDef{3}, {}, {}}, {"ns:c", {}, {}, {}}});
	auto bytes = encode(command, limits);
	CommandDecoder decoder(bytes, limits);
	CommandWorkBudget insufficient{2047, 1, 8, 32};
	decoder.step(insufficient);
	assert(insufficient.bytes == 2047 && insufficient.records == 1 && !decoder.done());
	assert(drive(decoder) > 16);
	auto validated = decoder.take();
	assert(!decoder.done() && throws([&] { decoder.take(); }));
	assert(encode(validated.command(), limits) == bytes);
	assert(validated.canonical_digest() == digest(command, limits));
	assert(validated.limits() == limits);
	std::size_t expected_bytes = 19;
	for (const auto &[key, value] : definition.overrides)
		expected_bytes += 8 + key.size() + 1 + (std::holds_alternative<bool>(value) ? 1 : 8);
	assert(validated.definition_bytes(0) == expected_bytes);
	assert(validated.definition_bytes(1) == 9 && validated.definition_bytes(2) == 0);
	auto close = open();
	close.kind = Kind::Close;
	close.nonce = {};
	close.generation = 1;
	for (auto small : {open(), close, transaction(1, {{"ns:a", BoolDef{}, {}, {}}})}) {
		auto canonical = encode(small);
		// Independent old decoder is the oracle. Every accepted corruption
		// must round-trip exactly, including scalar flags and framing.
		for (std::size_t offset = 0; offset < canonical.size(); ++offset) {
			auto corrupt = canonical;
			corrupt[offset] ^= 0x80;
			bool old_accepts = !throws([&] { decode(corrupt); });
			CommandDecoder cursor(corrupt);
			bool accepts = !throws([&] { drive(cursor); });
			assert(accepts == old_accepts);
			if (accepts) {
				auto value = cursor.take();
				assert(encode(value.command()) == corrupt);
				assert(value.canonical_digest() == digest(decode(corrupt)));
			} else {
				assert(throws([&] { cursor.step(insufficient); }));
				bool drained = false;
				for (unsigned turn = 0; !drained; ++turn) {
					CommandWorkBudget budget{2048, 1, 8, 32};
					drained = cursor.discard(budget);
					assert(turn < 1000);
				}
			}
		}
	}
	for (auto malformed : {bytes + "trailing", bytes.substr(0, bytes.size() - 1)}) {
		CommandDecoder cursor(malformed, limits);
		assert(throws([&] { drive(cursor); }));
		unsigned turns = 0;
		for (;;) {
			CommandWorkBudget budget{2048, 1, 8, 32};
			if (cursor.discard(budget))
				break;
			assert(++turns < 1000);
		}
		assert(turns >= 256);
	}
	CommandDecoder pending(bytes, limits);
	CommandWorkBudget first{8192, 2, 8, 32};
	pending.step(first); // Header and mutation, before its first override.
	auto moved = std::move(pending);
	assert(!pending.done() && pending.discard(first));
	assert(drive(moved) > 16);
	assert(moved.take().canonical_digest() == digest(command, limits));
	for (auto [mutations, overrides, padding] : {std::tuple{128u, 31u, 40u}, std::tuple{1u, 256u, 39u}}) {
		BoolDef large;
		for (unsigned i = 0; i < overrides; ++i)
			large.overrides["group" + std::to_string(i) + std::string(padding, 'x')] = GroupRollout{25};
		std::vector<Mutation> operations;
		for (unsigned i = 0; i < mutations; ++i)
			operations.push_back({"ns:feature_" + std::to_string(i), large, {}, {}});
		auto maximum = transaction(1, std::move(operations));
		auto canonical = encode(maximum);
		CommandDecoder cursor(canonical);
		assert(drive(cursor) > 16);
		auto completed = cursor.take();
		assert(completed.canonical_digest() == digest(maximum));
		assert(encode(completed.command()) == canonical);
		if (mutations == 128)
			assert(canonical.size() > Limits{}.request_bytes * 98 / 100);
		else
			assert(completed.definition_bytes(0) > Limits{}.definition_bytes * 98 / 100);
	}
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
			for (auto lower : {-1, key, 5000}) {
				auto range = tree.cursor_from(lower);
				auto reference = expected.lower_bound(lower);
				auto changed = tree.set(-2, turn);
				changed.collect(128);
				while (auto row = range.next()) {
					assert(reference != expected.end());
					assert(*row->first == reference->first && *row->second == reference->second);
					++reference;
				}
				assert(reference == expected.end());
			}
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
	for (const auto &command : commands)
		exact_size += 8 + encode(command, limits).size();
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
	BatchDecoder slices(bytes, limits);
	BatchApplication resumable(original, 1, 2, slices.count());
	while (!resumable.done()) {
		if (!resumable.command_pending()) {
			CommandDecoder command(slices.next_bytes(), limits);
			while (!command.done()) {
				CommandWorkBudget budget;
				command.step(budget);
				assert(original.checkpoint() == before);
			}
			resumable.start(command.take());
		}
		CommandWorkBudget budget;
		budget.updates = 1;
		budget.receipts = 1;
		resumable.step(budget);
		assert(original.checkpoint() == before);
	}
	assert(slices.done());
	auto resumed = resumable.finish();
	assert(resumed.results == results && resumed.state.checkpoint() == expected.checkpoint());
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
	for (std::size_t rows : {std::size_t{1}, std::size_t{128}}) {
		for (std::size_t fragment : {std::size_t{1}, std::size_t{73}, std::size_t{65536}}) {
			ImageDecoder decoder(context());
			std::size_t offset = 0;
			while (!decoder.finished()) {
				if (decoder.needs_input()) {
					auto count = std::min(fragment, bytes.size() - offset);
					decoder.feed(std::string_view(bytes).substr(offset, count), offset + count == bytes.size());
					offset += count;
				}
				decoder.step(rows);
			}
			auto recovered = decoder.take();
			assert(decoder.finished() && !decoder.needs_input());
			decoder.step(1);
			assert(!recovered.reclamation_pending());
			assert(recovered.checkpoint() == bytes);
			assert(recovered.retained_nodes() < recovered.size() + 1024);
		}
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
	assert(!invalid.needs_input() && !invalid.finished());
	assert(throws([&] { invalid.step(); }));
	assert(throws([&] { invalid.feed({}); }));
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
	deferred_values();
	command_continuations();
	command_encoding();
	prepared_proposals();
	bounded_applications();
	incremental_batches();
	streaming_images();
	semantics();
	std::cout << "authority state histories passed\n";
}
