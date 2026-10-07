#include "detent_distribution/retirement.hh"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
namespace d = detent::distribution;
void require(bool value) {
	if (!value)
		throw std::runtime_error("retirement invariant failed");
}
template <class Predicate> void eventually(Predicate predicate) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!predicate()) {
		if (std::chrono::steady_clock::now() >= deadline)
			throw std::runtime_error("retirement deadline exceeded");
		std::this_thread::yield();
	}
}
struct Owner : d::RetirementNode {
	d::RetirementRuntime &runtime;
	std::atomic<unsigned> &destroyed;
	std::atomic<bool> &wrong_thread;
	Owner(d::RetirementRuntime &runner, std::atomic<unsigned> &count, std::atomic<bool> &wrong)
		: runtime(runner), destroyed(count), wrong_thread(wrong) {}
	~Owner() {
		if (!runtime.on_worker())
			wrong_thread.store(true);
		destroyed.fetch_add(1);
	}
};
struct Family : Owner {
	std::array<d::RetirementHandle<Owner>, 16> children;
	std::size_t index = 0;
	using Owner::Owner;
	bool retire_step() noexcept override {
		if (index == children.size())
			return true;
		children[index++].reset();
		return false;
	}
};
struct Pause {
	std::atomic<bool> entered{false}, release{false};
	static void hook(void *context) noexcept {
		auto &pause = *static_cast<Pause *>(context);
		pause.entered.store(true, std::memory_order_release);
		while (!pause.release.load(std::memory_order_acquire))
			std::this_thread::yield();
	}
};
int main() {
	try {
		auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		std::atomic<unsigned> destroyed{0};
		std::atomic<bool> wrong_thread{false};
		{
			d::RetirementRuntime runtime(baseline, 2);
			require(baseline->used() == runtime.fixed_heap_capacity());
			require(runtime.configured_stack_bytes() == 1024 * 1024);
			{
				auto domain = runtime.domain(account, 17);
				auto family = domain.make<Family>(runtime, destroyed, wrong_thread);
				for (auto &child : family->children)
					child = domain.make<Owner>(runtime, destroyed, wrong_thread);
				require(domain.residents() == 17);
				bool pressure = false;
				try {
					domain.make<Owner>(runtime, destroyed, wrong_thread);
				} catch (const d::StoragePressure &) {
					pressure = true;
				}
				require(pressure);
				auto copy = family;
				family.reset();
				auto drain = domain.close();
				require(!drain.done() && destroyed == 0);
				bool closed = false;
				try {
					domain.make<Owner>(runtime, destroyed, wrong_thread);
				} catch (const std::logic_error &) {
					closed = true;
				}
				require(closed);
				copy.reset();
				drain.wait();
				require(domain.residents() == 0 && destroyed == 17 && !wrong_thread);
			}
			eventually([&] { return account->used() == 0; });
			{
				auto domain = runtime.domain(account);
				Pause pause;
				d::detail::RetirementTesting::before_ready(domain, Pause::hook, &pause);
				auto owner = domain.make<Owner>(runtime, destroyed, wrong_thread);
				auto drain = domain.close();
				std::jthread releaser([held = std::move(owner)]() mutable { held.reset(); });
				struct Unpause {
					Pause &pause;
					~Unpause() { pause.release.store(true, std::memory_order_release); }
				} unpause{pause};
				eventually([&] { return pause.entered.load(std::memory_order_acquire); });
				// Give the worker several ticks to consume the wake and detach the
				// unready mailbox head. It must retain the node and its domain.
				std::this_thread::sleep_for(std::chrono::milliseconds(80));
				require(!drain.done() && domain.residents() == 1 && destroyed == 17);
				pause.release.store(true, std::memory_order_release);
				drain.wait();
				require(destroyed == 18 && !wrong_thread);
			}
			eventually([&] { return account->used() == 0; });
			{
				auto first = runtime.domain(account), second = runtime.domain(account);
				auto a = first.make<Owner>(runtime, destroyed, wrong_thread);
				auto b = second.make<Owner>(runtime, destroyed, wrong_thread);
				auto first_drain = first.close(), second_drain = second.close();
				require(runtime.occupied_domains() == 2);
				bool pressure = false;
				try {
					runtime.domain(account);
				} catch (const d::StoragePressure &) {
					pressure = true;
				}
				require(pressure);
				a.reset();
				first_drain.wait();
				auto replacement = runtime.domain(account);
				auto replacement_drain = replacement.close();
				replacement_drain.wait();
				require(!second_drain.done());
				// The second owner remains valid long after facade close.
				auto late_copy = b;
				b.reset();
				require(!second_drain.done());
				std::jthread original([held = std::move(late_copy)]() mutable { held.reset(); });
				second_drain.wait();
			}
			eventually([&] { return account->used() == 0 && runtime.occupied_domains() == 0; });
			{
				auto zero = std::make_shared<d::StorageLedger>(0);
				bool pressure = false;
				try {
					runtime.domain(zero);
				} catch (const d::StoragePressure &) {
					pressure = true;
				}
				require(pressure && runtime.occupied_domains() == 0);
				auto domain = runtime.domain(account);
				const auto controls = account->used();
				struct Failing : d::RetirementNode {
					Failing() { throw std::runtime_error("owner constructor failure"); }
				};
				bool failed = false;
				try {
					domain.make<Failing>();
				} catch (const std::runtime_error &) {
					failed = true;
				}
				require(failed && domain.residents() == 0 && account->used() == controls);
				auto drain = domain.close();
				drain.wait();
			}
			for (unsigned i = 0; i < 100; ++i) {
				auto domain = runtime.domain(account);
				auto owner = domain.make<Owner>(runtime, destroyed, wrong_thread);
				auto drain = domain.close();
				owner.reset();
				drain.wait();
				require(baseline->used() == runtime.fixed_heap_capacity());
			}
			runtime.shutdown();
			require(account->used() == 0 && !wrong_thread);
			bool stopped = false;
			try {
				runtime.domain(account);
			} catch (const std::logic_error &) {
				stopped = true;
			}
			require(stopped);
		}
		require(baseline->used() == 0 && account->used() == 0);
		std::cout << "intrusive retirement, handoff races and bounded domains passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
