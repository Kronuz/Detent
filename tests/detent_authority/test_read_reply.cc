#include "detent/json_schema_internal.hh"
#include "read_reply.hh"
#include <cassert>
#include <limits>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace detent;
using namespace detent::authority;
namespace {
// Independent existing DOM encoder; preserve its definition implementation.
std::string oracle(const State &state, const std::vector<std::string> &keys) {
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
	w.StartObject();
	w.Key("namespace_revision");
	w.Uint64(state.namespace_revision("ns"));
	w.Key("key_revisions");
	w.StartObject();
	for (const auto &key : keys)
		if (auto r = state.find(key)) {
			w.Key(key.data(), key.size());
			w.Uint64(r->revision);
		}
	w.EndObject();
	w.Key("schema_version");
	w.Uint(1);
	w.Key("version");
	w.Uint64(state.revision());
	w.Key("knobs");
	w.StartObject();
	for (const auto &key : keys)
		if (auto r = state.find(key)) {
			w.Key(key.data(), key.size());
			rapidjson::Document d;
			auto value = detent::internal::write_knob_def(*r->value, d.GetAllocator());
			value.Accept(w);
		}
	w.EndObject();
	w.EndObject();
	return "{\"authority\":{\"cluster\":\"63000000000000000000000000000000\",\"group\":"
		   "\"67000000000000000000000000000000\",\"epoch\":\"65000000000000000000000000000000\"}," +
		   std::string(buffer.GetString() + 1, buffer.GetSize() - 1);
}
void count(ReadReplyEncoder &encoder) {
	unsigned turns = 0;
	while (!encoder.counted()) {
		CommandWorkBudget b{2048, 1, 0, 0};
		encoder.step(b);
		assert(++turns < 100000);
	}
	assert(!encoder.done());
}
std::string finish(ReadReplyEncoder &encoder) {
	encoder.begin_encoding();
	unsigned turns = 0;
	while (!encoder.done()) {
		CommandWorkBudget b{2048, 1, 0, 0};
		encoder.step(b);
		assert(++turns < 100000);
	}
	return encoder.take();
}
} // namespace
int main() {
	Context context;
	context.cluster[0] = 'c';
	context.group[0] = 'g';
	context.epoch[0] = 'e';
	State state(context);
	Command open;
	open.context = context;
	open.kind = Kind::Open;
	open.producer = "writer";
	open.nonce[0] = 'n';
	assert(state.apply(1, open).status == Status::Applied);
	BoolDef boolean;
	boolean.overrides[std::string("a\0\"\\\n", 5)] = true;
	boolean.overrides["é雪"] = GroupRollout{37};
	boolean.overrides["false"] = false;
	Command write;
	write.context = context;
	write.producer = "writer";
	write.generation = 1;
	write.sequence = 1;
	write.name_space = "ns";
	write.mutations.push_back({"ns:bool", KnobDef{boolean}, {}, {}});
	write.mutations.push_back({"ns:min", IntDef{std::numeric_limits<std::int64_t>::min()}, {}, {}});
	write.mutations.push_back({"ns:max", IntDef{std::numeric_limits<std::int64_t>::max()}, {}, {}});
	assert(state.apply(2, write).status == Status::Applied);
	for (auto keys : {std::vector<std::string>{},
					  std::vector<std::string>{"ns:max", "ns:missing", "ns:bool", "ns:min"}}) {
		const auto expected = oracle(state, keys);
		ReadReplyEncoder encoder(state, "ns", keys, context, expected.size());
		count(encoder);
		assert(encoder.encoded_size() == expected.size());
		assert(finish(encoder) == expected);
		bool rejected = false;
		try {
			encoder.take();
		} catch (const std::logic_error &) {
			rejected = true;
		}
		assert(rejected);
		ReadReplyEncoder short_encoder(state, "ns", keys, context, expected.size() - 1);
		rejected = false;
		try {
			count(short_encoder);
		} catch (const std::length_error &) {
			rejected = true;
		}
		assert(rejected);
	}
	auto frozen = state;
	std::vector<std::string> keys{"ns:bool"};
	const auto expected = oracle(frozen, keys);
	Context foreign = context;
	foreign.epoch[0] = 'x';
	bool context_rejected = false;
	try {
		ReadReplyEncoder wrong(frozen, "ns", keys, foreign, expected.size());
	} catch (const std::invalid_argument &) {
		context_rejected = true;
	}
	assert(context_rejected);

	ReadReplyEncoder encoder(frozen, "ns", keys, context, expected.size());
	count(encoder);
	write.sequence = 2;
	write.mutations.clear();
	write.mutations.push_back({"ns:bool", {}, {}, {}});
	assert(state.apply(3, write).status == Status::Applied);
	assert(finish(encoder) == expected);
	CommandWorkBudget budget;
	assert(encoder.discard(budget));
	// A maximum legal read keeps exactly one old arena across replacement.
	State large(context);
	assert(large.apply(1, open).status == Status::Applied);
	BoolDef dense;
	for (unsigned i = 0; i < 256; ++i)
		dense.overrides["group" + std::to_string(i) + std::string(46, 'k')] = true;
	dense.overrides.begin()->second = GroupRollout{100};
	std::vector<std::string> many_keys;
	for (unsigned i = 0; i < 128; ++i) {
		Command c;
		c.context = context;
		c.producer = "writer";
		c.generation = 1;
		c.sequence = i + 1;
		c.name_space = "ns";
		many_keys.push_back("ns:large" + std::to_string(i));
		c.mutations.push_back({many_keys.back(), KnobDef{dense}, {}, {}});
		assert(large.apply(i + 2, c).status == Status::Applied);
	}
	auto old = large;
	const auto many_expected = oracle(old, many_keys);
	ReadReplyEncoder many(old, "ns", many_keys, context, many_expected.size());
	count(many);
	large = State::restore(large.checkpoint(), context);
	assert(!old.shares_reclamation_with(large));
	assert(finish(many) == many_expected);
	assert(many.discard(budget));
	// Abandon both counting and encoding while replacement owns new arenas.
	for (bool encoding : {false, true}) {
		auto cancelled = large;
		ReadReplyEncoder cursor(cancelled, "ns", many_keys, context, many_expected.size());
		if (encoding) {
			count(cursor);
			cursor.begin_encoding();
		}
		CommandWorkBudget quantum{1024, 1, 0, 0};
		cursor.step(quantum);
		large = State::restore(large.checkpoint(), context);
		assert(!cancelled.shares_reclamation_with(large));
		assert(cursor.discard(quantum));
		cancelled.retire();
		unsigned turns = 0;
		while (cancelled.reclamation_pending()) {
			CommandWorkBudget b{2048, 1, 0, 1};
			cancelled.collect(b);
			assert(++turns < 100000);
		}
		assert(turns > 32768 && cancelled.retired_values() == 0);
	}
	old.retire();
	unsigned cleanup_turns = 0;
	while (old.reclamation_pending()) {
		CommandWorkBudget b{2048, 1, 0, 1};
		old.collect(b);
		assert(++cleanup_turns < 100000);
	}
	assert(cleanup_turns > 32768 && old.retired_values() == 0 && old.retired_value_bytes() == 0);
	frozen.retire();
	while (frozen.reclamation_pending()) {
		CommandWorkBudget b;
		frozen.collect(b);
	}
}
