#pragma once
#include "client.hh"
#include "completion_scheduler.hh"
#include "detent_local/disk_store.hh"
#include "publication_sink.hh"
#include <asio/thread_pool.hpp>
#include <completion/asio_completion.h>
#include <completion/native_completion.h>
#include <array>
#include <vector>

namespace detent::delivery {
namespace detail {
struct CoordinatorAccess;
}
enum class DeliveryMode { Poll, Subscription };
enum class LocalImageFormat { FlatV1, PagedV2 };
struct CoordinatorOptions {
	ClientOptions source;
	DeliveryMode delivery_mode = DeliveryMode::Poll;
	std::filesystem::path directory;
	local::ViewLimits view_limits;
	// Trusted previous identity permitted by configuration, never by input bytes.
	std::optional<distribution::Cursor> reset_from;
	std::chrono::milliseconds poll_interval{200};
	std::shared_ptr<PublicationSink> publication_sink;
	std::optional<CompletionPort> completion_port;
	std::chrono::milliseconds reconnect_maximum{0};
	std::uint64_t reconnect_seed = 0;
	LocalImageFormat image_format = LocalImageFormat::PagedV2;
	std::chrono::milliseconds compaction_interval{60000};
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
struct CleanupProgress {
	std::chrono::steady_clock::time_point last_resume{}, finished{};
	std::uint64_t turns = 0, charged_bytes = 0, maximum_gap_ns = 0;
};
struct CoordinatorStatus {
	CoordinatorPhase phase = CoordinatorPhase::Starting;
	local::SelectionSnapshot selected;
	std::optional<distribution::Cursor> received, validated, acknowledged, recoverable;
	std::uint64_t failures = 0;
	bool initialized = false;
	bool prepared_ready = false;
	bool worker_pending = false;
	std::exception_ptr error;
	std::size_t source_endpoint_index = 0;
	std::uint64_t source_rotations = 0, quorum_refresh_failures = 0;
	std::optional<std::chrono::steady_clock::time_point> last_quorum_open;
	std::chrono::steady_clock::time_point publication_acknowledged_at{}, discard_started_at{},
		recovery_started_at{};
	// Publication sink, image builder, then decoded input.
	std::array<CleanupProgress, 3> cleanup;
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
	asio::awaitable<void> run();
	void cancel_update() noexcept;
	void close() noexcept;
	bool drained() const noexcept;
	CoordinatorStatus status() const noexcept;

  private:
	friend struct detail::CoordinatorAccess;
	static asio::awaitable<void> run_owned(std::shared_ptr<Coordinator>);
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
std::shared_ptr<Coordinator> make_coordinator(CoordinatorOptions, std::shared_ptr<local::DiskStore>,
											  std::shared_ptr<NativeDriver>, distribution::RetirementDomain &,
											  asio::thread_pool &);
std::shared_ptr<Coordinator> prepare_coordinator(CoordinatorOptions, std::shared_ptr<NativeDriver>,
												 distribution::RetirementDomain &, asio::thread_pool &);
} // namespace detent::delivery
