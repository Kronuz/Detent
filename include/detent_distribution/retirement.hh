#pragma once
#include "allocator.hh"
#include <atomic>
#include <type_traits>

namespace detent::distribution {
class RetirementNode;
class RetirementDomain;
class RetirementFactory;
class RetirementRuntime;
namespace detail {
struct RetirementState;
struct RetirementRuntimeState;
void retain(RetirementNode *) noexcept;
void release(RetirementNode *) noexcept;
void admit_resident(RetirementState &);
void undo_resident(RetirementState &) noexcept;
std::shared_ptr<StorageLedger> retirement_account(RetirementState &);
void bind_retirement(RetirementNode &, RetirementState &, void (*)(RetirementNode *) noexcept) noexcept;
// Qualification hook: install before exposing owners, never concurrently.
struct RetirementTesting {
	static void before_ready(RetirementDomain &, void (*)(void *) noexcept, void *);
};
} // namespace detail

class RetirementNode {
  protected:
	RetirementNode() = default;
	virtual ~RetirementNode() = default;
	// Override for an owner containing multiple pages or child handles.
	// One true result permits final destruction on the cleanup worker.
	// False means a bounded work unit completed with more cleanup remaining;
	// accepted IO must retain its handle until settlement, before retirement.
	virtual bool retire_step() noexcept { return true; }
	// A failed cleanup can retain ownership and use the periodic retry queue.
	virtual bool retry_later() const noexcept { return false; }

  private:
	RetirementNode(const RetirementNode &) = delete;
	RetirementNode &operator=(const RetirementNode &) = delete;
	std::atomic<std::uint64_t> refs_{1};
	std::atomic<bool> ready_{false};
	RetirementNode *next_ = nullptr, *worker_next_ = nullptr;
	detail::RetirementState *domain_ = nullptr;
	void (*destroy_)(RetirementNode *) noexcept = nullptr;
	friend void detail::retain(RetirementNode *) noexcept;
	friend void detail::release(RetirementNode *) noexcept;
	friend void detail::bind_retirement(RetirementNode &, detail::RetirementState &,
										void (*)(RetirementNode *) noexcept) noexcept;
	friend struct detail::RetirementState;
	friend class RetirementDomain;
	friend class RetirementFactory;
};

// Library-owned references never allocate, destroy payloads or free a control
// block on their final caller. An admitted resident anchors the raw domain.
template <class T> class RetirementHandle {
  public:
	RetirementHandle() = default;
	RetirementHandle(const RetirementHandle &other) noexcept : node_(other.node_) {
		if (node_)
			detail::retain(node_);
	}
	RetirementHandle(RetirementHandle &&other) noexcept : node_(std::exchange(other.node_, nullptr)) {}
	RetirementHandle &operator=(RetirementHandle other) noexcept {
		std::swap(node_, other.node_);
		return *this;
	}
	~RetirementHandle() { reset(); }
	void reset() noexcept {
		if (auto node = std::exchange(node_, nullptr))
			detail::release(node);
	}
	explicit operator bool() const noexcept { return node_ != nullptr; }
	T *get() const noexcept { return static_cast<T *>(node_); }
	T *operator->() const noexcept { return get(); }
	T &operator*() const noexcept { return *get(); }

  private:
	explicit RetirementHandle(RetirementNode *node) noexcept : node_(node) {}
	RetirementNode *node_ = nullptr;
	friend class RetirementDomain;
	friend class RetirementFactory;
};

class RetirementDrain {
  public:
	bool done() const noexcept;
	// Explicit application-shutdown wait. Forbidden on the cleanup worker.
	void wait() const;

  private:
	explicit RetirementDrain(std::shared_ptr<detail::RetirementState> state) : state_(std::move(state)) {}
	std::shared_ptr<detail::RetirementState> state_;
	friend class RetirementDomain;
};

// Copyable admission capability; only the domain facade closes admission.
class RetirementFactory {
  public:
	RetirementFactory(const RetirementFactory &) noexcept = default;
	RetirementFactory(RetirementFactory &&) noexcept = default;
	RetirementFactory &operator=(const RetirementFactory &) noexcept = default;
	RetirementFactory &operator=(RetirementFactory &&) noexcept = default;
	template <class T, class... Args> RetirementHandle<T> make(Args &&...args) const {
		static_assert(std::is_base_of_v<RetirementNode, T>);
		if (!state_)
			throw std::logic_error("moved retirement factory");
		auto state = state_;
		detail::admit_resident(*state);
		StorageAllocator<T> allocator(detail::retirement_account(*state));
		T *owner = nullptr;
		try {
			owner = allocator.allocate(1);
			std::construct_at(owner, std::forward<Args>(args)...);
		} catch (...) {
			if (owner)
				allocator.deallocate(owner, 1);
			detail::undo_resident(*state);
			throw;
		}
		detail::bind_retirement(*owner, *state, [](RetirementNode *node) noexcept {
			auto object = static_cast<T *>(node);
			StorageAllocator<T> allocation(detail::retirement_account(*node->domain_));
			std::destroy_at(object);
			allocation.deallocate(object, 1);
		});
		return RetirementHandle<T>(owner);
	}

  private:
	explicit RetirementFactory(std::shared_ptr<detail::RetirementState> state) : state_(std::move(state)) {}
	std::shared_ptr<detail::RetirementState> state_;
	friend class RetirementDomain;
};

class RetirementDomain {
  public:
	RetirementDomain(RetirementDomain &&) noexcept = default;
	RetirementDomain(const RetirementDomain &) = delete;
	RetirementDomain &operator=(RetirementDomain &&) = delete;
	~RetirementDomain();
	RetirementDrain close() noexcept;
	std::size_t residents() const noexcept;
	std::shared_ptr<StorageLedger> storage_account() const;
	RetirementFactory factory() const {
		if (!state_)
			throw std::logic_error("moved retirement domain");
		return RetirementFactory(state_);
	}
	template <class T, class... Args> RetirementHandle<T> make(Args &&...args) {
		return factory().make<T>(std::forward<Args>(args)...);
	}

  private:
	explicit RetirementDomain(std::shared_ptr<detail::RetirementState> state) : state_(std::move(state)) {}
	std::shared_ptr<detail::RetirementState> state_;
	friend class RetirementRuntime;
	friend struct detail::RetirementTesting;
};

// One fixed runner and a charged fixed registry. The application owns this
// runtime and keeps it alive until all domains drain. Domain/view destruction
// never joins a thread. shutdown() is permanent and may wait for pinned owners.
class RetirementRuntime {
  public:
	explicit RetirementRuntime(std::shared_ptr<StorageLedger> baseline_account, std::size_t domains = 8,
							   std::size_t stack_bytes = 1024 * 1024);
	~RetirementRuntime();
	RetirementRuntime(const RetirementRuntime &) = delete;
	RetirementDomain domain(std::shared_ptr<StorageLedger> account, std::size_t owners = 4096);
	void shutdown();
	bool on_worker() const noexcept;
	std::size_t occupied_domains() const noexcept;
	std::size_t fixed_heap_capacity() const noexcept;
	std::size_t configured_stack_bytes() const noexcept;

  private:
	std::shared_ptr<StorageLedger> baseline_account_;
	PagedStorage storage_;
	detail::RetirementRuntimeState *state_ = nullptr;
};
} // namespace detent::distribution
