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
			d::SubscriptionLimits limits{1, 1};
			limits.idle_lifetime = std::chrono::milliseconds(100);
			limits.maximum_lifetime = std::chrono::seconds(1);
			Table table(account, limits);
			d::Cursor head{identity(1), identity(2), identity(3), {}, 10};
			head.scope.fill('s');
			d::SubscriptionPrincipal principal;
			principal.identity.fill('p');
			principal.policy_epoch = 1;
			const auto now = Table::Clock::now();
			check(table.open(identity(1), principal, 0, head, {}, false, false, {}, now) ==
				  d::SubscriptionResult::Accepted);
			auto request = table.next(identity(1), principal, 0, {}, now);
			check(request.token.has_value());
			table.collect(1, now + std::chrono::milliseconds(200));
			auto offered = table.offer(*request.token, head, 7, now + std::chrono::milliseconds(200));
			table.collect(1, now + std::chrono::milliseconds(400));
			check(table.metadata(identity(1), now + std::chrono::milliseconds(400)));
			table.settle(offered, true, now + std::chrono::milliseconds(400));
			check(table.metadata(identity(1), now + std::chrono::milliseconds(499)));
			check(!table.metadata(identity(1), now + std::chrono::milliseconds(500)));
			table.collect(1, now + std::chrono::milliseconds(500));
			check(table.drained());
			auto begin = [&](Table::Clock::time_point at) {
				check(table.open(identity(1), principal, 0, head, {}, false, false, {}, at) ==
					  d::SubscriptionResult::Accepted);
				auto admitted = table.next(identity(1), principal, 0, {}, at);
				check(admitted.token.has_value());
				return *admitted.token;
			};
			const auto maximum = now + std::chrono::seconds(2);
			auto expired = begin(maximum);
			check(table.next(identity(1), principal, 0, {}, maximum + std::chrono::milliseconds(999)).result ==
				  d::SubscriptionResult::Busy);
			check(!table.metadata(identity(1), maximum + std::chrono::seconds(1)));
			table.release(expired, maximum + std::chrono::seconds(1));
			table.collect(1, maximum + std::chrono::seconds(1));
			check(table.drained());
			const auto transfer = now + std::chrono::seconds(4);
			auto active = begin(transfer);
			auto window = table.offer(active, head, 7, transfer);
			check(!table.metadata(identity(1), transfer + std::chrono::seconds(1)));
			table.settle(window, true, transfer + std::chrono::seconds(1));
			table.collect(1, transfer + std::chrono::seconds(1));
			check(table.drained());
			const auto feedback = now + std::chrono::seconds(6);
			active = begin(feedback);
			window = table.offer(active, head, 7, feedback + std::chrono::milliseconds(200));
			auto pending = table.next(identity(1), principal, window.window, head,
									  feedback + std::chrono::milliseconds(250));
			check(pending.token.has_value());
			table.settle(window, true, feedback + std::chrono::milliseconds(650));
			table.collect(1, feedback + std::chrono::milliseconds(800));
			check(table.metadata(identity(1), feedback + std::chrono::milliseconds(800)));
			table.release(*pending.token, feedback + std::chrono::milliseconds(850));
			check(table.metadata(identity(1), feedback + std::chrono::milliseconds(949)));
			check(!table.metadata(identity(1), feedback + std::chrono::milliseconds(950)));
			table.collect(1, feedback + std::chrono::milliseconds(950));
			check(table.drained());
			const auto failed = now + std::chrono::seconds(8);
			active = begin(failed);
			window = table.offer(active, head, 7, failed);
			table.settle(window, false, failed);
			check(!table.metadata(identity(1), failed));
			table.collect(1, failed);
			check(table.drained());
			const auto closing = now + std::chrono::seconds(10);
			active = begin(closing);
			check(table.close(identity(1), principal) == d::SubscriptionResult::Accepted);
			table.collect(1, closing);
			check(!table.drained());
			table.release(active, closing);
			table.collect(1, closing);
			check(table.drained());
			const auto stale = now + std::chrono::seconds(12);
			check(table.open(identity(1), principal, 0, head, {}, false, false, {}, stale) ==
				  d::SubscriptionResult::Accepted);
			auto forbidden = principal;
			forbidden.identity.fill('x');
			check(table.next(identity(1), forbidden, 0, {}, stale + std::chrono::milliseconds(80)).result ==
				  d::SubscriptionResult::Forbidden);
			table.release(active, stale + std::chrono::milliseconds(90));
			check(!table.metadata(identity(1), stale + std::chrono::milliseconds(100)));
			table.collect(1, stale + std::chrono::milliseconds(100));
			check(table.drained());
		}
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
