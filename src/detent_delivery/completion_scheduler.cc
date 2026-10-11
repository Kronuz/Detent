#include "detent_delivery/completion_scheduler.hh"
#include <vector>

namespace detent::delivery {
namespace d = distribution;
struct CompletionScheduler::Impl {
	struct Slot {
		asio::steady_timer wake;
		std::shared_ptr<kronuz::journal::IOOperation> operation;
		bool granted = false;
		std::chrono::steady_clock::time_point queued_at;
		explicit Slot(asio::any_io_executor executor) : wake(executor) {}
	};
	asio::any_io_executor executor;
	std::shared_ptr<NativeDriver> driver;
	std::vector<Slot, d::StorageAllocator<Slot>> slots;
	std::vector<std::size_t, d::StorageAllocator<std::size_t>> queue;
	std::size_t head = 0, waiting = 0;
	unsigned turns = 0;
	bool active = false;
	std::size_t peak_waiting = 0;
	std::uint64_t grants = 0, maximum_wait_nanoseconds = 0;
	Impl(asio::any_io_executor owner, std::shared_ptr<NativeDriver> completion,
		 std::shared_ptr<d::StorageLedger> account, std::size_t participants)
		: executor(std::move(owner)), driver(std::move(completion)),
		  slots(d::StorageAllocator<Slot>(account)), queue(d::StorageAllocator<std::size_t>(account)) {
		if (!driver || participants == 0 || participants > 256)
			throw std::invalid_argument("invalid completion scheduler participants");
		slots.reserve(participants);
		queue.resize(participants);
		for (std::size_t i = 0; i < participants; ++i)
			slots.emplace_back(executor);
	}
	void grant(std::size_t index) noexcept {
		++grants;
		const auto wait = std::chrono::duration_cast<std::chrono::nanoseconds>(
							  std::chrono::steady_clock::now() - slots[index].queued_at)
							  .count();
		maximum_wait_nanoseconds = std::max(maximum_wait_nanoseconds, static_cast<std::uint64_t>(wait));
		slots[index].granted = true;
	}
	void release(std::size_t index) noexcept {
		slots[index].operation.reset();
		slots[index].granted = false;
		active = waiting != 0;
		if (active) {
			auto next = queue[head];
			head = (head + 1) % queue.size();
			--waiting;
			grant(next);
			slots[next].wake.cancel();
		}
	}
};
CompletionScheduler::CompletionScheduler(asio::any_io_executor executor, std::shared_ptr<NativeDriver> driver,
										 std::shared_ptr<d::StorageLedger> account, std::size_t participants)
	: impl_(d::make_storage_owner<Impl>(account, std::malloc, std::free, std::move(executor),
										std::move(driver), account, participants)) {}
CompletionScheduler::~CompletionScheduler() = default;
bool CompletionScheduler::idle() const noexcept { return !impl_->active && impl_->waiting == 0; }
CompletionSchedulerStatus CompletionScheduler::status() const noexcept {
	return {impl_->slots.size(),
			impl_->waiting,
			impl_->peak_waiting,
			impl_->grants,
			impl_->maximum_wait_nanoseconds,
			impl_->active};
}
CompletionPort CompletionScheduler::port(std::size_t index) {
	if (index >= impl_->slots.size())
		throw std::out_of_range("completion participant");
	CompletionPort result;
	result.scheduler_ = shared_from_this();
	result.index_ = index;
	return result;
}
bool CompletionPort::uses(const std::shared_ptr<NativeDriver> &driver) const noexcept {
	return scheduler_ && scheduler_->impl_->driver == driver;
}
asio::awaitable<void> CompletionPort::run_one(std::shared_ptr<kronuz::journal::IOOperation> operation) {
	if (!scheduler_)
		throw std::logic_error("unbound completion participant");
	return CompletionScheduler::run_owned(scheduler_, index_, std::move(operation));
}
asio::awaitable<void>
CompletionScheduler::run_owned(std::shared_ptr<CompletionScheduler> lifetime, std::size_t index,
							   std::shared_ptr<kronuz::journal::IOOperation> operation) {
	auto &state = *lifetime->impl_;
	if (co_await asio::this_coro::executor != state.executor)
		throw std::logic_error("completion scheduler requires its owner executor");
	auto &slot = state.slots.at(index);
	if (!operation || slot.operation || operation->in_flight())
		throw std::logic_error("completion participant is not available");
	co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
	slot.operation = std::move(operation);
	slot.queued_at = std::chrono::steady_clock::now();
	slot.wake.expires_at(std::chrono::steady_clock::time_point::max());
	if (!state.active) {
		state.active = true;
		state.grant(index);
	} else {
		state.queue[(state.head + state.waiting) % state.queue.size()] = index;
		++state.waiting;
		state.peak_waiting = std::max(state.peak_waiting, state.waiting);
	}
	while (!slot.granted) {
		asio::error_code error;
		co_await slot.wake.async_wait(asio::redirect_error(asio::use_awaitable, error));
		if (error && error != asio::error::operation_aborted)
			throw asio::system_error(error);
	}
	struct Release {
		Impl &state;
		std::size_t index;
		~Release() { state.release(index); }
	} release{state, index};
	if (++state.turns == 16) {
		state.turns = 0;
		co_await asio::post(state.executor, asio::use_awaitable);
	}
	// Cancellation may finish or rewrite the operation while it waits its turn.
	co_await state.driver->run_one(slot.operation);
}
} // namespace detent::delivery
