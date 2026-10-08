#include "change_history.hh"
#include "detent_distribution/messagepack.hh"
#include <iostream>
#include <source_location>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace m = d::messagepack;
namespace {
void check(bool condition, std::source_location location = std::source_location::current()) {
	if (!condition)
		throw std::runtime_error("change history invariant at line " + std::to_string(location.line()));
}
a::ValidatedCommand validated(const a::Command &command) {
	auto bytes = a::encode(command);
	a::CommandDecoder decoder(bytes);
	while (!decoder.done()) {
		a::CommandWorkBudget budget;
		decoder.step(budget);
	}
	return decoder.take();
}
void drain(a::ChangeHistory &history) {
	unsigned turns = 0;
	while (history.pending()) {
		a::CommandWorkBudget budget;
		budget.bytes = 1;
		budget.receipts = 1;
		history.collect(budget);
		check(++turns < 100000);
	}
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
		command.name_space = "ns";
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto history = std::make_shared<a::ChangeHistory>(account, a::HistoryOptions{2, 4096, 1024});
		history->reset(context, 0);
		std::uint64_t index = 1;
		auto apply = [&](bool publish = true) {
			const auto before = state.revision();
			history->begin_batch(context, ++index, before);
			a::BatchApplication batch(state, index, 1, 1, history);
			batch.start(validated(command));
			unsigned turns = 0;
			while (!batch.done()) {
				a::CommandWorkBudget budget;
				budget.bytes = 8192;
				budget.records = 1;
				batch.step(budget);
				check(state.revision() == before && history->through() == before);
				check(++turns < 100000);
			}
			auto result = batch.finish();
			check(result.results.front().status == a::Status::Applied);
			state = std::move(result.state);
			if (publish)
				history->publish(context, index, state.revision());
		};
		command.sequence = 1;
		command.mutations = {{"ns:a", detent::IntDef{1}, {}, {}}};
		apply();
		check(history->covers(context, 0, 1) && history->size() == 1);
		auto held = history->find(1);
		check(held && held->name_space() == "ns" && held->mutations == 1);
		std::string expected;
		m::array(expected, 4);
		m::unsigned_integer(expected, 1);
		m::unsigned_integer(expected, 1);
		m::string(expected, "ns");
		m::array(expected, 1);
		m::array(expected, 3);
		m::string(expected, "ns:a");
		m::unsigned_integer(expected, 0);
		m::array(expected, 2);
		m::unsigned_integer(expected, 1);
		m::signed_integer(expected, 1);
		check(held->body->view() == expected);
		apply(); // Idempotent retry has no new revision or cache entry.
		check(history->size() == 1 && history->through() == 1);
		command.sequence = 2;
		command.mutations = {{"ns:a", detent::IntDef{1}, {}, {}}, {"ns:b", detent::IntDef{2}, {}, {}}};
		apply();
		check(history->find(2)->mutations == 1);
		command.sequence = 3;
		command.mutations = {{"ns:a", {}, {}, {}}};
		apply();
		drain(*history);
		check(!history->covers(context, 0, 3) && history->covers(context, 1, 3));
		check(!history->find(1) && held->body->view() == expected);
		const auto retained = account->used();
		held.reset();
		check(account->used() < retained);
		command.sequence = 4;
		command.mutations = {{"ns:b", detent::IntDef{4}, {}, {}}};
		apply(false);
		check(history->through() == 3 && !history->find(4));
		history->abandon_batch();
		drain(*history);
		history->publish(context, index, state.revision());
		drain(*history);
		check(history->coverage_floor() == 4 && !history->covers(context, 3, 4));
		// A missing capture must invalidate coverage even without an explicit gap.
		history->begin_batch(context, ++index, 4);
		history->publish(context, index, 5);
		check(history->coverage_floor() == 5 && !history->covers(context, 4, 5));
		auto replacement = context;
		replacement.epoch.fill('x');
		history->reset(replacement, 0);
		drain(*history);
		check(!history->covers(context, 0, 0) && history->covers(replacement, 0, 0));
		history.reset();
		check(account->used() == 0);
		// Optional encoding pressure cannot reject an authoritative transaction.
		history = std::make_shared<a::ChangeHistory>(account, a::HistoryOptions{2, 4096, 1});
		history->reset(context, state.revision());
		command.sequence = 5;
		command.mutations = {{"ns:b", detent::IntDef{5}, {}, {}}};
		apply();
		drain(*history);
		check(state.revision() == 5 && history->size() == 0 && history->coverage_floor() == 5);
		history.reset();
		check(account->used() == 0);
		// Exercise a complete draft separately with tiny budgets, then cancel
		// before staging. Both its sealed body and controls must remain charged.
		history = std::make_shared<a::ChangeHistory>(account);
		history->reset(context, state.revision());
		command.sequence = 6;
		detent::BoolDef flag;
		flag.default_value = true;
		flag.rollout_pct = 25;
		flag.overrides.emplace("blue", detent::GroupRollout{50});
		flag.overrides.emplace("green", false);
		command.mutations = {{"ns:flag", flag, {}, {}}};
		a::State candidate(state);
		check(candidate.apply(++index, command).status == a::Status::Applied);
		history->begin_batch(context, index, state.revision());
		const std::array<unsigned char, 1> changed{1};
		auto draft = history->capture(candidate, command, changed);
		unsigned turns = 0;
		while (true) {
			a::CommandWorkBudget budget;
			budget.bytes = 1;
			budget.records = 1;
			if (draft->step(budget))
				break;
			check(++turns < 100000);
		}
		const auto charged = account->used();
		a::CommandWorkBudget zero;
		zero.bytes = 0;
		check(!draft->discard(zero) && account->used() == charged);
		turns = 0;
		while (true) {
			a::CommandWorkBudget budget;
			budget.bytes = 1;
			budget.receipts = 1;
			if (draft->discard(budget))
				break;
			check(++turns < 100000);
		}
		check(turns > 1 && account->used() < charged);
		draft.reset();
		history->abandon_batch();
		history.reset();
		check(account->used() == 0);
		std::cout << "canonical payload, staged publication, retries, eviction, pinned accounting, gaps and "
					 "pressure passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
