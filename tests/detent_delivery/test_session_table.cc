#include "detent_distribution/session_table.hh"
#include <iostream>
#include <source_location>

namespace d = detent::distribution;
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("session table invariant at " + std::to_string(where.line()));
}
d::Identity identity(unsigned number) {
	d::Identity value{};
	value[0] = 1;
	value[14] = static_cast<char>(number >> 8);
	value[15] = static_cast<char>(number);
	return value;
}
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
		{
			using Table = d::SessionTable<std::uint64_t, std::uint32_t>;
			Table table(account, {500, 500});
			d::Cursor head{identity(1), identity(2), identity(3), {}, 10};
			head.scope.fill('s');
			d::SubscriptionPrincipal principal;
			principal.identity.fill('p');
			principal.policy_epoch = 1;
			auto now = Table::Clock::now();
			auto continuous = [](const auto &marker, const auto &, const auto &) noexcept {
				return marker == 7;
			};
			for (unsigned i = 1; i <= 500; ++i) {
				check(table.open(identity(i), principal, 0, head, {}, false, false, {}, now) ==
					  d::SubscriptionResult::Accepted);
				auto request = table.next(identity(i), principal, 0, {}, now);
				check(request.token && table.poll(*request.token, head, continuous, now) ==
										   d::SubscriptionResult::SnapshotReady);
				auto offered = table.offer(*request.token, head, 7, now);
				check(table.next(identity(i), principal, 1, {}, now).result == d::SubscriptionResult::Busy);
				auto feedback = table.next(identity(i), principal, 1, head, now);
				check(feedback.token && table.poll(*feedback.token, head, continuous, now) ==
											d::SubscriptionResult::AwaitSettlement);
				table.settle(offered, true);
				check(table.poll(*feedback.token, head, continuous, now) == d::SubscriptionResult::Parked);
				auto behind = head;
				--behind.revision;
				check(table.poll(*feedback.token, behind, continuous, now) ==
					  d::SubscriptionResult::SourceBehind);
				check(table.poll(*feedback.token, head, continuous, now) == d::SubscriptionResult::Parked);
				table.release(*feedback.token);
			}
			auto ahead = head;
			++ahead.revision;
			check(table.open(identity(501), principal, 0, head, ahead, false, false, 7, now) ==
				  d::SubscriptionResult::SourceBehind);
			check(table.size() == 500);
			check(table.open(identity(501), principal, 0, head, {}, false, false, {}, now) ==
				  d::SubscriptionResult::Full);
			auto changed = principal;
			++changed.policy_epoch;
			check(table.next(identity(1), changed, 1, head, now).result ==
				  d::SubscriptionResult::IdentityChanged);
			auto request = table.next(identity(1), principal, 1, head, now);
			check(request.token.has_value());
			auto lost = [](const auto &, const auto &, const auto &) noexcept { return false; };
			check(table.poll(*request.token, head, lost, now) == d::SubscriptionResult::SnapshotRequired);
			table.close_all();
			d::WorkBudget budget{8192, 500};
			table.collect(budget, now);
			check(table.size() == 1 && !table.drained());
			table.release(*request.token);
			budget = {8192, 500};
			table.collect(budget, now);
			check(table.drained());
		}
		{
			using Table = d::SessionTable<std::uint64_t, std::uint32_t>;
			Table table(account, {1, 1});
			d::Cursor head{identity(1), identity(2), identity(3), {}, 10};
			head.scope.fill('s');
			d::SubscriptionPrincipal principal;
			principal.identity.fill('p');
			principal.policy_epoch = 1;
			auto now = Table::Clock::now();
			check(table.open(identity(1), principal, 2, head, {}, false, false, {}, now) ==
				  d::SubscriptionResult::Accepted);
			auto old = table.next(identity(1), principal, 0, {}, now);
			check(old.token.has_value());
			auto offered = table.offer(*old.token, head, 7, now);
			check(table.close(identity(1), principal) == d::SubscriptionResult::Accepted);
			d::WorkBudget budget;
			table.collect(budget, now);
			check(!table.drained());
			table.settle(offered, true);
			budget = {};
			table.collect(budget, now);
			check(table.drained());
			check(table.open(identity(1), principal, 3, head, {}, false, false, {}, now) ==
				  d::SubscriptionResult::Accepted);
			auto fresh = table.next(identity(1), principal, 0, {}, now);
			check(fresh.token && fresh.token->incarnation != old.token->incarnation);
			table.release(*old.token);
			table.settle(offered, false);
			check(table.metadata(identity(1), now) && table.metadata(identity(1), now)->scope == 3);
			check(table.next(identity(1), principal, 0, {}, now).result == d::SubscriptionResult::Busy);
			table.close_all();
			budget = {};
			table.collect(budget, now);
			check(!table.drained());
			table.release(*fresh.token);
			budget = {};
			table.collect(budget, now);
			check(table.drained());
		}
		check(account->used() == 0);
		std::cout << "500 independent windows, source lag, gaps and retained requests passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
