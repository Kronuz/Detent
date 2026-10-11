#include "detent_delivery/completion_scheduler.hh"
#include "detent_distribution/memory_resource.hh"
#include <atomic>
#include <iostream>
#include <thread>

namespace t = detent::delivery;
namespace d = detent::distribution;
namespace j = kronuz::journal;
void check(bool value) {
	if (!value)
		throw std::runtime_error("scheduler invariant");
}
struct FakeIO final : j::IO {
	std::atomic<bool> entered{false}, release{false};
	std::vector<std::string> calls;
	std::unique_ptr<j::OwnerLock> acquire_owner(bool) override { return {}; }
	std::unique_ptr<j::File> open_existing(std::string_view) override { return {}; }
	std::unique_ptr<j::File> create_exclusive(std::string_view) override { return {}; }
	void replace(std::string_view, std::string_view) override {}
	void remove(std::string_view name) override { calls.emplace_back(name); }
	void sync_directory() override {
		entered.store(true);
		while (!release.load())
			std::this_thread::yield();
		calls.emplace_back("held");
	}
};
struct Operation final : j::IOOperation {
	FakeIO &backend;
	j::MutationRequest command;
	bool finished = false, pending = false;
	unsigned submissions = 0;
	Operation(FakeIO &io, std::string_view name, bool held = false) : backend(io) {
		command.kind = held ? j::PrimitiveKind::DirectorySync : j::PrimitiveKind::Remove;
		command.source = name;
		command.token.operation = reinterpret_cast<std::uintptr_t>(this);
	}
	const j::MutationRequest &request() const override { return command; }
	void submitted() override {
		check(!pending && !finished);
		pending = true;
		++submissions;
	}
	bool complete(j::MutationCompletion result) noexcept override {
		if (!pending || result.token != command.token || result.error)
			std::terminate();
		pending = false;
		finished = true;
		return true;
	}
	bool done() const noexcept override { return finished; }
	bool in_flight() const noexcept override { return pending; }
	j::IO &io() const noexcept override { return backend; }
};
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		asio::io_context owner;
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		auto driver = std::make_shared<t::NativeDriver>(owner.get_executor(), queue);
		auto scheduler = std::allocate_shared<t::CompletionScheduler>(
			d::StorageAllocator<t::CompletionScheduler>(account), owner.get_executor(), driver, account, 3);
		FakeIO io;
		auto first = std::make_shared<Operation>(io, "", true);
		auto second = std::make_shared<Operation>(io, "stale");
		auto third = std::make_shared<Operation>(io, "cancelled");
		unsigned completed = 0;
		bool duplicate = false, responsive = false;
		auto completion = [&](std::exception_ptr error) {
			if (error)
				std::rethrow_exception(error);
			++completed;
		};
		auto repeat = [&](std::size_t index, std::shared_ptr<Operation> operation,
						  std::string_view next) -> asio::awaitable<void> {
			co_await scheduler->port(index).run_one(operation);
			operation->finished = false;
			operation->command.kind = j::PrimitiveKind::Remove;
			operation->command.source = next;
			co_await scheduler->port(index).run_one(operation);
		};
		asio::co_spawn(owner, repeat(0, first, "first-again"), completion);
		asio::co_spawn(owner, repeat(1, second, "second-again"), completion);
		asio::co_spawn(owner, scheduler->port(2).run_one(third), completion);
		asio::co_spawn(owner, scheduler->port(1).run_one(second), [&](std::exception_ptr error) {
			try {
				if (error)
					std::rethrow_exception(error);
			} catch (const std::logic_error &) {
				duplicate = true;
			}
		});
		asio::steady_timer timer(owner);
		auto release_original = [&]() -> asio::awaitable<void> {
			while (!io.entered.load()) {
				timer.expires_after(std::chrono::milliseconds(1));
				co_await timer.async_wait(asio::use_awaitable);
			}
			check(completed == 0 && !scheduler->idle() && duplicate);
			responsive = true;
			second->command.source = "cleanup";
			third->finished = true;
			io.release.store(true);
		};
		asio::co_spawn(owner, release_original(), [](std::exception_ptr error) {
			if (error)
				std::rethrow_exception(error);
		});
		owner.run();
		check(completed == 3 && responsive && scheduler->idle());
		check((io.calls == std::vector<std::string>{"held", "cleanup", "first-again", "second-again"}));
		check(first->submissions == 2 && second->submissions == 2 && third->submissions == 0);
		const auto diagnostics = scheduler->status();
		check(diagnostics.grants == 5 && diagnostics.peak_queued == 2 &&
			  diagnostics.maximum_wait_nanoseconds > 0);
		check(queue->stats().maximum_outstanding == 1);
		scheduler.reset();
		owner.restart();
		scheduler = std::allocate_shared<t::CompletionScheduler>(
			d::StorageAllocator<t::CompletionScheduler>(account), owner.get_executor(), driver, account, 256);
		std::weak_ptr<t::CompletionScheduler> weak = scheduler;
		for (std::size_t index = 0; index < 256; ++index) {
			auto done = std::make_shared<Operation>(io, "never-submitted");
			done->finished = true;
			asio::co_spawn(owner, scheduler->port(index).run_one(done), completion);
		}
		scheduler.reset();
		check(!weak.expired());
		owner.run();
		check(completed == 259 && weak.expired());
		bool refused = false;
		try {
			t::CompletionScheduler invalid(owner.get_executor(), driver, account, 257);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused);
		const auto stats = queue->stats();
		std::cout << "FIFO, queued cleanup/cancellation, facade loss and 256 participants passed; native="
				  << stats.native_submitted << ", fallback=" << stats.fallback_submitted << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
