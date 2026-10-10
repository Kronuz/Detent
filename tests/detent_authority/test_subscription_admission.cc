#include "subscription_admission.hh"
#include <array>
#include <atomic>
#include <iostream>
#include <source_location>
#include <thread>

namespace a = detent::authority;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("subscription admission invariant at " + std::to_string(where.line()));
}
d::ScopeIdentity principal(char value) {
	d::ScopeIdentity result{};
	result.fill(value);
	return result;
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
		{
			a::SubscriptionRequests requests(account, {1, 2, 1, 1, 1, 4096, 1024});
			const auto idle = account->used();
			auto first = requests.admit(principal('a'), a::SubscriptionRequestKind::Next);
			check(first && !requests.drained());
			check(first->body().storage_capacity() >= requests.body_limit(a::SubscriptionRequestKind::Next));
			first->body().append("{\"version\":1}");
			first->body().seal_prefix();
			check(first->body().view() == "{\"version\":1}");
			check(!requests.admit(principal('a'), a::SubscriptionRequestKind::Next));
			auto second = requests.admit(principal('b'), a::SubscriptionRequestKind::Next);
			check(second && !requests.admit(principal('c'), a::SubscriptionRequestKind::Next));
			auto opened = requests.admit(principal('a'), a::SubscriptionRequestKind::Open);
			check(opened && !requests.admit(principal('c'), a::SubscriptionRequestKind::Open));
			auto closed = requests.admit(principal('a'), a::SubscriptionRequestKind::Close);
			check(closed && !requests.admit(principal('c'), a::SubscriptionRequestKind::Close));
			opened.reset();
			closed.reset();
			second.reset();
			auto original = first;
			first.reset();
			check(!requests.admit(principal('a'), a::SubscriptionRequestKind::Next));
			const auto held = account->used();
			requests.close();
			check(!requests.admit(principal('c'), a::SubscriptionRequestKind::Close));
			check(!requests.drained() && account->used() == held);
			original.reset();
			check(requests.drained() && account->used() == idle);
		}
		check(account->used() == 0);
		{
			a::SubscriptionRequests requests(account, {2, 2, 2, 2, 2, 4096, 1024});
			auto first = requests.admit(principal('a'), a::SubscriptionRequestKind::Next);
			const auto used = account->used();
			{
				auto full = account->admit(account->limit() - used);
				bool refused = false;
				try {
					requests.admit(principal('b'), a::SubscriptionRequestKind::Next);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				check(refused);
			}
			check(account->used() == used);
			auto second = requests.admit(principal('b'), a::SubscriptionRequestKind::Next);
			check(bool(second));
			first.reset();
			second.reset();
			check(requests.drained());
		}
		check(account->used() == 0);
		{
			a::SubscriptionRequests requests(account, {8, 8, 8, 1, 1, 4096, 1024});
			const auto idle = account->used();
			std::atomic<bool> valid{true};
			std::array<std::thread, 8> threads;
			for (unsigned i = 0; i < threads.size(); ++i)
				threads[i] = std::thread([&, i] {
					try {
						for (unsigned turn = 0; turn < 1000; ++turn) {
							auto lease = requests.admit(principal('a' + i), a::SubscriptionRequestKind::Next);
							if (!lease ||
								requests.admit(principal('a' + i), a::SubscriptionRequestKind::Next)) {
								valid = false;
								return;
							}
							lease->body().append("{}");
							lease->body().seal_prefix();
						}
					} catch (...) {
						valid = false;
					}
				});
			for (auto &thread : threads)
				thread.join();
			check(valid && requests.drained() && account->used() == idle);
		}
		check(account->used() == 0);
		std::cout
			<< "separate poll/control admission, physical bodies and retained-original drainage passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
