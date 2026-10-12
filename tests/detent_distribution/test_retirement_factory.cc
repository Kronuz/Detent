#include "detent_distribution/retirement.hh"
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
namespace d = detent::distribution;
void require(bool value) {
	if (!value)
		throw std::runtime_error("retirement factory invariant failed");
}
template <class Error, class Call> void rejects(Call call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("retirement factory admission unexpectedly succeeded");
}
template <class Test> void eventually(Test test) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!test()) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
}
struct Empty : d::RetirementNode {};
struct Owner : d::RetirementNode {
	d::RetirementRuntime &runtime;
	std::atomic<unsigned> &destroyed;
	std::atomic<bool> &wrong_thread;
	Owner(d::RetirementRuntime &runner, std::atomic<unsigned> &count, std::atomic<bool> &wrong)
		: runtime(runner), destroyed(count), wrong_thread(wrong) {}
	~Owner() {
		if (!runtime.on_worker())
			wrong_thread = true;
		++destroyed;
	}
};
struct Pause {
	std::atomic<bool> entered{false}, release{false};
};
struct Blocking : d::RetirementNode {
	d::RetirementRuntime &runtime;
	std::atomic<unsigned> &destroyed;
	std::atomic<bool> &wrong_thread;
	Blocking(d::RetirementRuntime &runtime, std::atomic<unsigned> &count, std::atomic<bool> &wrong,
			 Pause &pause, bool fail)
		: runtime(runtime), destroyed(count), wrong_thread(wrong) {
		pause.entered = true;
		while (!pause.release.load())
			std::this_thread::yield();
		if (fail)
			throw std::invalid_argument("constructor failure");
	}
	~Blocking() {
		if (!runtime.on_worker())
			wrong_thread = true;
		++destroyed;
	}
};
int main() {
	auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
	auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
	std::optional<d::RetirementFactory> retained;
	std::atomic<unsigned> destroyed{0};
	std::atomic<bool> wrong_thread{false};
	{
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 1);
		auto factory = domain.factory();
		const auto copy = factory;
		auto moved_domain = std::move(domain);
		rejects<std::logic_error>([&] { domain.factory(); });
		auto owner = copy.make<Owner>(runtime, destroyed, wrong_thread);
		rejects<d::StoragePressure>([&] { factory.make<Empty>(); });
		owner.reset();
		eventually([&] { return moved_domain.residents() == 0; });
		auto pressure = account->admit(account->limit() - account->used());
		rejects<d::StoragePressure>([&] { factory.make<Empty>(); });
		pressure.reset();
		require(moved_domain.residents() == 0);
		retained.emplace(std::move(factory));
		rejects<std::logic_error>([&] { factory.make<Empty>(); });
		moved_domain.close().wait();
		rejects<std::logic_error>([&] { retained->make<Empty>(); });
		require(destroyed == 1 && !wrong_thread);
	}
	rejects<std::logic_error>([&] { retained->make<Empty>(); });
	retained.reset();
	eventually([&] { return account->used() == 0 && baseline->used() == 0; });
	{
		d::RetirementRuntime runtime(baseline, 1);
		{
			auto domain = runtime.domain(account, 1);
			retained.emplace(domain.factory());
		}
		rejects<std::logic_error>([&] { retained->make<Empty>(); });
		runtime.shutdown();
	}
	rejects<std::logic_error>([&] { retained->make<Empty>(); });
	retained.reset();
	eventually([&] { return account->used() == 0 && baseline->used() == 0; });
	for (bool fail : {false, true}) {
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 1);
		auto factory = domain.factory();
		Pause pause;
		std::optional<d::RetirementHandle<Blocking>> result;
		std::atomic<bool> failed{false}, stopped{false};
		std::jthread creator([&] {
			try {
				result.emplace(factory.make<Blocking>(runtime, destroyed, wrong_thread, pause, fail));
			} catch (const std::invalid_argument &) {
				failed = true;
			}
		});
		eventually([&] { return pause.entered.load(); });
		std::jthread shutdown([&] {
			runtime.shutdown();
			stopped = true;
		});
		eventually([&] {
			try {
				factory.make<Empty>();
			} catch (const d::StoragePressure &) {
			} catch (const std::logic_error &) {
				return true;
			}
			return false;
		});
		require(!stopped);
		pause.release = true;
		creator.join();
		require(failed == fail && bool(result) != fail);
		result.reset();
		shutdown.join();
		rejects<std::logic_error>([&] { factory.make<Empty>(); });
	}
	require(destroyed == 2 && !wrong_thread);
	eventually([&] { return account->used() == 0 && baseline->used() == 0; });
}
