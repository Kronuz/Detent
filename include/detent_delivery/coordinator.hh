#pragma once
#include "client.hh"
#include "detent_distribution/allocator.hh"
#include "detent_local/disk_store.hh"
#include "hash_stats.hh"
#include "namespace_session.hh"
#include <asio/thread_pool.hpp>
#include <completion/asio_completion.h>
#include <completion/native_completion.h>
#include <array>
#include <vector>

namespace detent::delivery {
using NativeDriver = kronuz::journal::AsioCompletionDriver<kronuz::io::completion::NativeQueue>;
namespace detail {
struct CoordinatorAccess;
}
enum class LocalImageFormat { FlatV1, PagedV2 };
// The agent's delivery (namespace-delivery D8, W5, W6): one W5 session following every namespace,
// each response's frames built into one generation per namespace and published in one store cycle.
struct CoordinatorOptions {
	ClientOptions source;
	// The authority's cluster: every frame's cursor carries it.
	distribution::Identity cluster{};
	// The followed namespaces, strictly increasing; with `all`, every granted name as the directory
	// reaches it, beside these.
	std::vector<std::string> namespaces;
	bool all = false;
	std::filesystem::path directory;
	local::ViewLimits view_limits;
	// The first retry after a failure; failures in a row back off to `reconnect_maximum`.
	std::chrono::milliseconds retry_interval{200};
	std::chrono::milliseconds reconnect_maximum{0};
	std::uint64_t reconnect_seed = 0;
	LocalImageFormat image_format = LocalImageFormat::PagedV2;
	std::chrono::milliseconds compaction_interval{60000};
	// Pack scrub pace in bytes hashed per second, 0 to disable (namespace delivery D10.2).
	std::uint64_t scrub_bytes_per_second = 512 * 1024;
	std::vector<asio::ip::tcp::endpoint> authority_endpoints;
	std::chrono::milliseconds quorum_refresh{30000};
};
enum class CoordinatorPhase {
	Starting,
	Recovering,
	Receiving,
	Building,
	WaitingPublication,
	Publishing,
	Reconciling,
	Discarding,
	Closing,
	Closed,
	Fenced
};
struct ScrubStatus {
	std::uint64_t passes = 0, bytes_hashed = 0, damaged_packs = 0;
	// A pack of the selected generation differs from its owner record; reads of its pages fail first touch.
	bool source_failure = false;
};
// Repair of the selected generation (namespace design D10.3): requested by a scrub report of a damaged pack or
// a first-touch failure in the update path, completed when a repair publication is selected.
struct RepairStatus {
	std::uint64_t requested = 0, completed = 0;
	bool pending = false;
};
struct NamespaceStatus {
	std::string name;
	std::optional<distribution::NamespaceCursor> cursor; // none: no generation of it
	bool repair = false; // a full copy is asked for it (D10)
	bool operator==(const NamespaceStatus &) const = default;
};
struct CoordinatorStatus {
	CoordinatorPhase phase = CoordinatorPhase::Starting;
	local::SelectionSnapshot selected;
	std::vector<NamespaceStatus> namespaces;
	std::optional<distribution::DirectoryCursor> directory;
	// Responses whose frames were published, and the cycle that last published.
	std::uint64_t responses = 0, idle = 0, failures = 0;
	bool initialized = false;
	// Every followed namespace is published or the source answered for it (C4).
	bool ready = false;
	bool worker_pending = false;
	std::exception_ptr error;
	std::size_t source_endpoint_index = 0;
	std::uint64_t source_rotations = 0, quorum_refresh_failures = 0;
	std::optional<std::chrono::steady_clock::time_point> last_quorum_open;
	std::chrono::steady_clock::time_point recovery_started_at{};
	ScrubStatus scrub;
	RepairStatus repair;
	local::SyncStats sync;
	// Everything this process (the tier) has hashed.
	HashStats hash;
};
// One owner executor serializes status, cancellation and close. The host keeps
// the configuration worker, completion driver and retirement domain alive
// through run() settlement, then destroys/joins those resources off the owner.
class Coordinator : public std::enable_shared_from_this<Coordinator> {
  public:
	Coordinator(CoordinatorOptions, std::shared_ptr<local::DiskStore>, std::shared_ptr<NativeDriver>,
				distribution::RetirementDomain &, asio::thread_pool &);
	~Coordinator();
	Coordinator(const Coordinator &) = delete;
	// Startup result attachment, exactly once before run(). This lets hosts
	// admit coordinator controls before make_disk_store performs startup IO.
	void attach_store(std::shared_ptr<local::DiskStore>);
	task::task<void> run();
	void cancel_update() noexcept;
	void close() noexcept;
	bool drained() const noexcept;
	CoordinatorStatus status() const noexcept;

  private:
	friend struct detail::CoordinatorAccess;
	static task::task<void> run_owned(std::shared_ptr<Coordinator>);
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
std::shared_ptr<Coordinator> make_coordinator(CoordinatorOptions, std::shared_ptr<local::DiskStore>,
											  std::shared_ptr<NativeDriver>, distribution::RetirementDomain &,
											  asio::thread_pool &);
std::shared_ptr<Coordinator> prepare_coordinator(CoordinatorOptions, std::shared_ptr<NativeDriver>,
												 distribution::RetirementDomain &, asio::thread_pool &);
} // namespace detent::delivery
