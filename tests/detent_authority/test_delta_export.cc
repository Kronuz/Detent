#include "delta_export.hh"
#include "detent_distribution/messagepack.hh"
#include <iostream>
#include <source_location>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace m = d::messagepack;
namespace {
void check(bool value, std::source_location line = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("delta export invariant at " + std::to_string(line.line()));
}
void cursor(std::string &out, const d::Cursor &value) {
	m::array(out, 5);
	for (const auto *id : {&value.cluster, &value.group, &value.restore_epoch})
		m::string(out, {id->data(), id->size()}, true);
	m::string(out, {value.scope.data(), value.scope.size()}, true);
	m::unsigned_integer(out, value.revision);
}
std::string oracle(d::Cursor base, d::Cursor target, bool progress = false) {
	std::string out;
	m::array(out, progress ? 5 : 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, progress ? 3 : 2);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	cursor(out, base);
	cursor(out, target);
	if (progress)
		return out;
	m::array(out, 4);
	m::unsigned_integer(out, 2);
	m::unsigned_integer(out, 2);
	m::unsigned_integer(out, 0);
	m::unsigned_integer(out, 12);
	m::array(out, 2);
	for (auto revision : {1, 3}) {
		m::array(out, 3);
		m::unsigned_integer(out, revision);
		m::array(out, 1);
		m::array(out, 2);
		m::string(out, "ns");
		m::unsigned_integer(out, revision);
		m::array(out, 1);
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		m::string(out, "ns:a");
		m::unsigned_integer(out, revision == 1 ? 0 : 1);
		if (revision == 1) {
			m::array(out, 2);
			m::unsigned_integer(out, 1);
			m::signed_integer(out, 7);
		} else
			out.push_back('\xc0');
	}
	return out;
}
std::string emit(a::DeltaExportEncoder &encoder, std::size_t bytes = 1, unsigned *chunks = nullptr) {
	std::string out;
	unsigned turns = 0;
	while (!encoder.done()) {
		d::WorkBudget budget{bytes, 1};
		encoder.step(budget);
		if (encoder.chunk_ready()) {
			if (chunks)
				++*chunks;
			out.append(encoder.take_chunk()->view());
		}
		check(++turns < 1000000);
	}
	check(encoder.counted() && encoder.encoded_length() == out.size());
	return out;
}
} // namespace
int main() {
	try {
		a::Context context;
		context.cluster.fill('c');
		context.group.fill('g');
		context.epoch.fill('e');
		a::State state(context);
		a::Command command;
		command.context = context;
		command.producer = "writer";
		command.kind = a::Kind::Open;
		command.nonce.fill('n');
		check(state.apply(1, command).generation == 1);
		command.kind = a::Kind::Transaction;
		command.nonce = {};
		command.generation = 1;
		auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		auto history = std::make_shared<a::ChangeHistory>(account);
		history->reset(context, 0);
		const std::array<unsigned char, 1> changed{1};
		for (unsigned revision = 1; revision <= 3; ++revision) {
			command.sequence = revision;
			command.name_space = revision == 2 ? "secret" : "ns";
			command.mutations = {{revision == 2 ? "secret:x" : "ns:a",
								  revision == 3 ? std::optional<detent::KnobDef>{} : detent::IntDef{7},
								  {},
								  {}}};
			history->begin_batch(context, revision + 1, state.revision());
			check(state.apply(revision + 1, command).status == a::Status::Applied);
			auto draft = history->capture(state, command, changed);
			while (true) {
				a::CommandWorkBudget budget;
				if (draft->step(budget))
					break;
			}
			history->stage(draft);
			history->publish(context, revision + 1, state.revision());
		}
		std::vector<std::string> names{"ns"};
		auto scope = a::AuthorizedScope::authorize("reader", 1, names, {"ns", "secret"}, account);
		d::Cursor base{context.cluster, context.group, context.epoch, scope->identity(), 0};
		auto target = base;
		target.revision = 3;
		{
			a::DeltaExportEncoder encoder(history, scope, context, base, target);
			auto bytes = emit(encoder);
			check(encoder.kind() == d::UpdateKind::Delta && bytes == oracle(base, target));
			d::DeltaDecoder decoder(base, names);
			std::span<const char> input(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget budget{1, 1};
				decoder.feed(input, budget);
			}
			auto delta = decoder.finish();
			check(delta.target() == target && delta.transactions().size() == 2);
		}
		{
			auto from = base;
			from.revision = 1;
			auto to = target;
			to.revision = 2;
			a::DeltaExportEncoder encoder(history, scope, context, from, to);
			check(emit(encoder) == oracle(from, to, true) && encoder.kind() == d::UpdateKind::Progress);
		}
		for (unsigned limit = 0; limit < 4; ++limit) {
			d::Limits bounds;
			if (limit == 0)
				bounds.bytes = 1;
			if (limit == 1)
				bounds.records = 1;
			if (limit == 2)
				bounds.string_bytes = 1;
			if (limit == 3)
				bounds.definition_bytes = 1;
			a::DeltaExportEncoder encoder(history, scope, context, base, target, bounds);
			bool rejected = false;
			try {
				emit(encoder);
			} catch (const std::length_error &) {
				rejected = true;
			}
			check(rejected && !encoder.chunk_ready());
		}
		{
			a::DeltaExportEncoder encoder(history, scope, context, base, target);
			while (!encoder.counted()) {
				d::WorkBudget budget{8192, 1};
				encoder.step(budget);
			}
			const auto retained = account->used();
			d::WorkBudget zero{0, 1};
			check(!encoder.discard(zero) && account->used() == retained);
			unsigned turns = 0;
			while (true) {
				d::WorkBudget budget{1, 1};
				if (encoder.discard(budget))
					break;
				check(++turns < 1000000);
			}
			check(account->used() < retained && !encoder.chunk_ready());
		}
		{
			a::DeltaExportEncoder encoder(history, scope, context, base, target);
			while (!encoder.counted()) {
				d::WorkBudget budget{17, 1};
				encoder.step(budget);
			}
			history->reset(context, 3);
			while (history->pending()) {
				a::CommandWorkBudget budget;
				history->collect(budget);
			}
			check(emit(encoder) == oracle(base, target)); // Pinned bodies survive eviction.
		}
		bool refused = false;
		try {
			a::DeltaExportEncoder encoder(history, scope, context, base, target);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused);
		// More than one output chunk, including nested rollout overrides.
		detent::BoolDef flag;
		flag.default_value = true;
		flag.rollout_pct = 35;
		for (unsigned i = 0; i < 128; ++i)
			flag.overrides.emplace("group" + std::to_string(1000 + i),
								   i % 2 ? detent::GroupOverride{false}
										 : detent::GroupOverride{detent::GroupRollout{50}});
		for (unsigned revision = 4; revision <= 67; ++revision) {
			flag.default_value = revision % 2;
			command.sequence = revision;
			command.name_space = "ns";
			command.mutations = {{"ns:flag", flag, {}, {}}};
			history->begin_batch(context, revision + 1, state.revision());
			check(state.apply(revision + 1, command).status == a::Status::Applied);
			auto draft = history->capture(state, command, changed);
			while (true) {
				a::CommandWorkBudget budget;
				if (draft->step(budget))
					break;
			}
			history->stage(draft);
			history->publish(context, revision + 1, state.revision());
		}
		auto from = base;
		from.revision = 3;
		auto to = target;
		to.revision = 67;
		{
			a::DeltaExportEncoder encoder(history, scope, context, from, to);
			unsigned chunks = 0;
			auto bytes = emit(encoder, 127, &chunks);
			check(chunks > 1 && bytes.size() > 65536);
			d::DeltaDecoder decoder(from, names);
			std::span<const char> input(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget budget{127, 3};
				decoder.feed(input, budget);
			}
			auto delta = decoder.finish();
			check(delta.target() == to && delta.transactions().size() == 64);
			const auto &mutation = delta.mutation(delta.transactions().back().mutations.start);
			const auto &definition = std::get<d::WireBoolDef>(*mutation.value);
			check(definition.default_value && definition.rollout_pct == 35 &&
				  definition.overrides.count == 128);
		}
		{
			d::Limits bounds;
			bounds.overrides_per_definition = 127;
			a::DeltaExportEncoder encoder(history, scope, context, from, to, bounds);
			bool rejected = false;
			try {
				emit(encoder);
			} catch (const std::length_error &) {
				rejected = true;
			}
			check(rejected && !encoder.chunk_ready());
		}
		history->reset(context, 67);
		while (history->pending()) {
			a::CommandWorkBudget budget;
			history->collect(budget);
		}
		scope.reset();
		history.reset();
		check(account->used() == 0);
		std::cout
			<< "delta oracle, scoped progress, decoder round-trip, tiny budgets and pinned eviction passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
