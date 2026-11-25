#pragma once
// An agent on the W5/W6 test server: an owner loop, a disk store and the coordinator, with status
// polls on the owner and an orderly close.
#include "../../src/detent_delivery/coordinator_internal.hh"
#include "namespace_server_fixture.hh"
#include "detent/owner_loop.hh"
#include "detent_delivery/coordinator.hh"
#include "detent_distribution/memory_resource.hh"
#include "detent_local/reader.hh"
#include <filesystem>
#include <future>

namespace coordinator_agent {
namespace t = detent::delivery;
namespace l = detent::local;
using namespace namespace_server;
inline std::string message(const std::exception_ptr &error) {
	try {
		if (error)
			std::rethrow_exception(error);
	} catch (const std::exception &failure) {
		return failure.what();
	} catch (...) {
	}
	return "unknown";
}
struct Agent {
	detent::OwnerLoop owner;
	std::shared_ptr<l::DiskStore> store;
	std::shared_ptr<t::NativeDriver> driver;
	std::shared_ptr<t::Coordinator> coordinator;
	asio::thread_pool worker{1};
	Agent(const std::filesystem::path &root, l::StoreOpenMode mode, t::CoordinatorOptions options,
		  std::shared_ptr<d::StorageLedger> account, d::RetirementDomain &domain,
		  std::shared_ptr<kronuz::io::completion::NativeQueue> queue) {
		l::DiskStoreOptions disk;
		disk.mode = mode;
		disk.limits = {64 * 1024 * 1024, 100, 10};
		disk.recovery_limits = options.view_limits;
		store = l::make_disk_store(domain, root, account, disk);
		driver = std::allocate_shared<t::NativeDriver>(d::StorageAllocator<t::NativeDriver>(account), owner.scheduler(),
													   std::move(queue));
		options.source.scheduler = &owner.scheduler();
		coordinator = t::make_coordinator(std::move(options), store, driver, domain, worker);
		asio::post(owner.io(), [this] { task::spawn(task::scheduler_ref::of(owner.scheduler()), coordinator->run()); });
	}
	void repair(std::string_view name) {
		std::promise<void> done;
		asio::post(owner.io(), [&] {
			t::detail::CoordinatorAccess::request_repair(*coordinator, name);
			done.set_value();
		});
		done.get_future().get();
	}
	void fail_next_build(std::exception_ptr failure) {
		std::promise<void> done;
		asio::post(owner.io(), [&] {
			t::detail::CoordinatorAccess::fail_next_build(*coordinator, std::move(failure));
			done.set_value();
		});
		done.get_future().get();
	}
	t::CoordinatorStatus status() {
		std::promise<t::CoordinatorStatus> out;
		asio::post(owner.io(), [&] { out.set_value(coordinator->status()); });
		return out.get_future().get();
	}
	template <class Done>
	t::CoordinatorStatus until(Done done, const char *what, std::chrono::seconds wait = 30s) {
		for (const auto limit = std::chrono::steady_clock::now() + wait;; std::this_thread::sleep_for(5ms)) {
			auto state = status();
			if (state.phase == t::CoordinatorPhase::Fenced)
				throw std::runtime_error(std::string("coordinator fenced waiting for ") + what + ": " + message(state.error));
			if (done(state))
				return state;
			if (std::chrono::steady_clock::now() > limit) {
				std::string at;
				for (const auto &each : state.namespaces)
					at += " " + each.name + "@" + (each.cursor ? std::to_string(each.cursor->position) : "-");
				throw std::runtime_error(std::string("timed out waiting for ") + what + ": phase " +
										 std::to_string(int(state.phase)) + ", ready " + std::to_string(state.ready) +
										 ", responses " + std::to_string(state.responses) + ", failures " +
										 std::to_string(state.failures) + ", namespaces" + at + ", error " +
										 message(state.error));
			}
		}
	}
	~Agent() {
		asio::post(owner.io(), [this] { coordinator->close(); });
		for (const auto limit = std::chrono::steady_clock::now() + 30s; std::chrono::steady_clock::now() < limit;
			 std::this_thread::sleep_for(5ms)) {
			std::promise<bool> drained;
			asio::post(owner.io(), [&] { drained.set_value(coordinator->drained()); });
			if (drained.get_future().get())
				break;
		}
		owner.stop();
		owner.close();
		owner.destroy_roots();
		worker.join();
		coordinator.reset();
		driver.reset();
		store.reset();
	}
};
// A reader attached to the namespace's entry by name sees the server's state at its head.
inline void matches(Server &server, const std::filesystem::path &root, const l::ViewLimits &limits, const std::string &name) {
	State expected;
	std::uint64_t head = 0;
	{
		std::lock_guard lock(server.mutex);
		const auto history = server.find(name).history();
		head = history.head().position;
		expected = history.state_at(head);
	}
	l::GenerationReader reader(root, limits, {}, {}, name);
	reader.refresh();
	const auto view = reader.read_view();
	check(view.cursor().position == head);
	std::uint32_t live = 0;
	for (const auto &[key, held] : expected) {
		const auto bare = std::string_view(key).substr(name.size() + 1);
		check(view.contains(name, bare) == held.value.has_value());
		if (!held.value)
			continue;
		++live;
		if (!held.value->rollout_pct && held.value->overrides.empty())
			check(view.get(name, bare).value() == held.value->default_value);
	}
	check(view.size() == live);
}
} // namespace coordinator_agent
