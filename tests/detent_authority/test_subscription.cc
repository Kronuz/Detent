#include "detent/scope.hh"
#include "subscription.hh"
#include <iostream>
#include <source_location>

namespace a = detent::authority;
namespace d = detent::distribution;
using namespace std::chrono_literals;
namespace {
void check(bool condition, std::source_location location = std::source_location::current()) {
	if (!condition)
		throw std::runtime_error("subscription invariant at " + std::to_string(location.line()));
}
d::Identity id(char value) {
	d::Identity result{};
	result.fill(value);
	return result;
}
a::SubscriptionPrincipal principal(std::string_view name) {
	return {d::canonical_scope_identity(name, 1, {}), 1};
}
void drain(a::Subscriptions &table, a::Subscriptions::Clock::time_point now) {
	for (unsigned turn = 0; !table.drained(); ++turn) {
		check(turn < 1000);
		table.collect(2, now);
	}
}
} // namespace
int main() {
	try {
		a::Context context{id('c'), id('g'), id('e')};
		auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
		a::ChangeHistory history(account);
		history.reset(context, 0);
		auto scope =
			a::AuthorizedScope::authorize("reader", 1, std::vector<std::string>{"ns"}, {"ns"}, account);
		auto reader = principal("reader");
		d::Cursor current{context.cluster, context.group, context.epoch, scope->identity(), 0};
		const auto now = a::Subscriptions::Clock::now();
		const auto foundation_charge = account->used();
		{
			a::Subscriptions table(account, {2, 1, 100ms, 200ms});
			check(table.open(id('a'), reader, scope, current, {}, false, false, history, now) ==
				  a::SubscriptionResult::Accepted);
			check(table.metadata(id('a'), now)->principal == reader);
			const auto incarnation = table.metadata(id('a'), now)->incarnation;
			auto grants = principal("grants").identity;
			check(table.authorize(id('a'), incarnation, principal("other"), scope->identity(), grants, now) ==
				  a::SubscriptionResult::Forbidden);
			check(!table.metadata(id('a'), now)->last_authorized_grants);
			check(table.authorize(id('a'), incarnation, reader, scope->identity(), grants, now) ==
				  a::SubscriptionResult::Accepted);
			check(table.metadata(id('a'), now)->last_authorized_grants == grants);
			check(table.open(id('b'), reader, scope, current, {}, false, false, history, now) ==
				  a::SubscriptionResult::Full);
			check(table.next(id('a'), principal("other"), 0, {}, now).result ==
				  a::SubscriptionResult::Forbidden);
			auto changed_policy = reader;
			++changed_policy.policy_epoch;
			check(table.next(id('a'), changed_policy, 0, {}, now).result ==
				  a::SubscriptionResult::IdentityChanged);
			auto initial = table.next(id('a'), reader, 0, {}, now);
			check(initial.token &&
				  table.poll(*initial.token, current, history, now) == a::SubscriptionResult::SnapshotReady);
			check(table.next(id('a'), reader, 0, {}, now).result == a::SubscriptionResult::Busy);
			auto offer = table.offer(*initial.token, current, history.handoff(context, 0), now);
			check(offer.window == 1);
			check(table.next(id('a'), reader, 0, {}, now).result == a::SubscriptionResult::Busy);
			auto wrong = current;
			wrong.revision = 1;
			check(table.next(id('a'), reader, 1, wrong, now).result == a::SubscriptionResult::OutOfSync);
			auto feedback = table.next(id('a'), reader, 1, current, now);
			check(feedback.token && table.poll(*feedback.token, current, history, now) ==
										a::SubscriptionResult::AwaitSettlement);
			table.release(*initial.token);
			check(table.poll(*feedback.token, current, history, now) ==
				  a::SubscriptionResult::AwaitSettlement);
			table.settle(offer, true);
			check(table.poll(*feedback.token, current, history, now) == a::SubscriptionResult::Parked);
			check(table.base(*feedback.token) == current && table.handoff(*feedback.token));
			table.release(*feedback.token);
			const auto idle_charge = account->used();
			for (unsigned i = 0; i < 1000; ++i) {
				auto duplicate = table.next(id('a'), reader, 1, current, now);
				check(duplicate.token &&
					  table.poll(*duplicate.token, current, history, now) == a::SubscriptionResult::Parked);
				table.release(*duplicate.token);
			}
			check(account->used() == idle_charge);
			history.reset(context, 0);
			check(history.handoff(context, 0).has_value());
			auto reset = table.next(id('a'), reader, 1, current, now);
			check(reset.token &&
				  table.poll(*reset.token, current, history, now) == a::SubscriptionResult::SnapshotRequired);
			table.collect(2, now);
			check(table.size() == 1);
			table.release(*reset.token);
			const auto retained = account->used();
			d::WorkBudget no_bytes{0, 2};
			table.collect(no_bytes, now);
			check(table.drained() && account->used() < retained);
			drain(table, now);
			check(table.drained());
			check(table.open(id('a'), reader, scope, current, current, true, false, history, now) ==
				  a::SubscriptionResult::Accepted);
			auto reopened = table.next(id('a'), reader, 0, {}, now);
			check(reopened.token && reopened.token->incarnation != initial.token->incarnation);
			check(table.authorize(id('a'), incarnation, reader, scope->identity(), grants, now) ==
				  a::SubscriptionResult::Expired);
			check(!table.metadata(id('a'), now)->last_authorized_grants);
			check(table.close(id('a'), reader, incarnation) == a::SubscriptionResult::Expired);
			table.release(*reset.token);
			check(table.poll(*reopened.token, current, history, now) == a::SubscriptionResult::SnapshotReady);
			auto held = table.offer(*reopened.token, current, history.handoff(context, 0), now);
			table.settle(offer, false);
			check(table.metadata(id('a'), now));
			check(table.close(id('a'), principal("other")) == a::SubscriptionResult::Forbidden);
			check(table.close(id('a'), reader) == a::SubscriptionResult::Accepted);
			table.collect(2, now + 300ms);
			check(table.size() == 1);
			table.settle(held, true);
			drain(table, now + 300ms);
			check(table.drained());
		}
		check(account->used() == foundation_charge);
		{
			a::Subscriptions table(account, {2, 2, 100ms, 200ms});
			auto future = current;
			future.revision = 1;
			check(table.open(id('a'), reader, scope, current, future, true, false, history, now) ==
				  a::SubscriptionResult::OutOfSync);
			auto foreign = current;
			foreign.restore_epoch = id('f');
			check(table.open(id('a'), reader, scope, current, foreign, false, false, history, now) ==
				  a::SubscriptionResult::IdentityChanged);
			check(table.open(id('a'), reader, scope, current, current, false, false, history, now) ==
				  a::SubscriptionResult::Accepted);
			const auto before = account->used();
			{
				auto saturated = account->admit(account->limit() - before);
				bool refused = false;
				try {
					table.open(id('b'), reader, scope, current, {}, false, false, history, now);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				check(refused && table.size() == 1);
			}
			check(account->used() == before);
			auto parked = table.next(id('a'), reader, 0, {}, now);
			check(parked.token &&
				  table.poll(*parked.token, current, history, now) == a::SubscriptionResult::Parked);
			table.close_all();
			check(table.poll(*parked.token, current, history, now) == a::SubscriptionResult::Expired);
			table.collect(2, now);
			check(table.size() == 1);
			table.release(*parked.token);
			drain(table, now);
			check(table.drained());
			check(table.open(id('b'), reader, scope, current, {}, false, false, history, now) ==
				  a::SubscriptionResult::Expired);
		}
		check(account->used() == foundation_charge);
		{
			a::State state(context);
			a::Command command;
			command.context = context;
			command.producer = "writer";
			command.kind = a::Kind::Open;
			command.nonce = id('n');
			check(state.apply(1, command).generation == 1);
			command.kind = a::Kind::Transaction;
			command.nonce = {};
			command.generation = 1;
			a::Subscriptions table(account, {2, 2, 100ms, 200ms});
			check(table.open(id('a'), reader, scope, current, current, false, false, history, now) ==
				  a::SubscriptionResult::Accepted);
			const std::array<unsigned char, 1> changed{1};
			std::optional<d::Cursor> published;
			for (unsigned revision = 1; revision <= 3; ++revision) {
				command.sequence = revision;
				command.name_space = revision == 2 ? "secret" : "ns";
				command.mutations = {{revision == 2 ? "secret:x" : "ns:a", detent::IntDef{revision}, {}, {}}};
				history.begin_batch(context, revision + 1, state.revision());
				check(state.apply(revision + 1, command).status == a::Status::Applied);
				auto draft = history.capture(state, command, changed);
				for (;;) {
					a::CommandWorkBudget work;
					if (draft->step(work))
						break;
				}
				history.stage(draft);
				history.publish(context, revision + 1, state.revision());
				auto target = current;
				target.revision = revision;
				auto next = table.next(id('a'), reader, revision - 1, published, now);
				check(next.token &&
					  table.poll(*next.token, target, history, now) == a::SubscriptionResult::ResumeReady);
				check(table.base(*next.token)->revision == revision - 1);
				check(table.handoff(*next.token)->revision() == revision - 1);
				auto offer = table.offer(*next.token, target, history.handoff(context, revision), now);
				check(offer.window == revision);
				table.settle(offer, true);
				published = target;
			}
			auto final = table.next(id('a'), reader, 3, published, now);
			check(final.token &&
				  table.poll(*final.token, *published, history, now) == a::SubscriptionResult::Parked);
			check(table.base(*final.token) == published);
			table.close_all();
			table.release(*final.token);
			drain(table, now);
		}
		{
			auto retirement_account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
			a::ChangeHistory empty_history(retirement_account);
			std::vector<std::string> names;
			std::set<std::string> grants;
			for (unsigned i = 0; i < 1000; ++i) {
				auto digits = std::to_string(i);
				names.push_back("ns" + std::string(4 - digits.size(), '0') + digits + std::string(190, 'x'));
				grants.insert(names.back());
			}
			auto large_scope = a::AuthorizedScope::authorize("reader", 1, names, grants, retirement_account);
			auto target = current;
			target.scope = large_scope->identity();
			const auto bytes = large_scope->storage_capacity();
			a::Subscriptions table(retirement_account, {1024, 1024, 100ms, 200ms});
			check(table.open(id('z'), reader, large_scope, target, {}, false, false, empty_history, now) ==
				  a::SubscriptionResult::Accepted);
			check(table.close(id('z'), reader) == a::SubscriptionResult::Accepted);
			large_scope.reset();
			std::size_t turns = 0;
			while (!table.drained()) {
				d::WorkBudget work{8192, 1};
				table.collect(work, now);
				check(++turns <= (bytes + 8191) / 8192);
			}
		}
		std::cout << "subscription publication windows, retained handoff, admission and drainage passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
