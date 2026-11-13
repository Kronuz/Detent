#pragma once
// A voter's history directory (namespace-delivery D6 "Commit", "Recovery"): seal
// files committed in seal order, roll-up outputs in one sync group, the `layout`
// table at period seals, and a collector that checks against the durable table.
// Writes queue in order and run as completion IO operations the host drives, so
// apply's seal computes and returns; reads see queued files before the disk's.
#include "history_layout.hh"
#include <completion/io.h>
#include <completion/operation.h>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>

namespace detent::authority::history {
// One directory under D8's rules: a file is written under `.tmp-<name>`, synced,
// then renamed to its name; an existing name is kept when its bytes match.
class Directory final : public Files {
  public:
	Directory(std::shared_ptr<kronuz::journal::IO> io, Limits limits);
	std::string read(std::string_view name) override;
	std::optional<std::string> find(std::string_view name);
	// Publishes the files as one sync group: written, synced, renamed, then the directory.
	void publish(const std::vector<std::pair<std::string, std::string>> &files);
	void remove(std::string_view name);
	std::vector<std::string> names();
	void remove_temporaries();
	const std::shared_ptr<kronuz::journal::IO> &io() const noexcept { return io_; }

  private:
	std::shared_ptr<kronuz::journal::IO> io_;
	Limits limits_;
};
using Group = std::vector<std::pair<std::string, std::string>>;
// One queued write: `.tmp-` names removed (a restore), the groups published in order,
// then the collector, which keeps what `table` reaches and the files `above` it (the
// seal files above the table and their outputs). Once done, seals through `durable` are.
struct Write {
	bool temporaries = false;
	std::vector<Group> groups;
	std::optional<Table> table;
	std::vector<std::string> above;
	std::uint64_t durable = 0, epoch = 0;
};
// Runs one write as the primitives a completion driver issues, one at a time.
std::shared_ptr<kronuz::journal::IOOperation> write_operation(std::shared_ptr<kronuz::journal::IO> io,
															  std::shared_ptr<const Write> write);
// Serves a peer's fetch: nullopt for a name history never writes (checked before the disk) or
// a file it lacks; a file above `bytes` throws unread.
std::optional<std::string> serve(kronuz::journal::IO &io, std::string_view name, std::size_t bytes);
// Whether a peer's bytes are the file wanted: its digest, or a seal file's or table's own.
using Check = std::function<bool(std::string_view bytes)>;
// A fetch's outcome: bytes that passed the check, or what each voter answered instead.
struct Reply {
	std::optional<std::string> bytes;
	std::string why;
};
// Asks the peers for a history file by name; `done` runs on the owner once with the first
// bytes that pass `good`, or with none once no peer can give them. Every voter holds the
// same bytes (D6 "Recovery").
using Fetched = std::function<void(Reply)>;
using Fetch = std::function<void(const std::string &name, Check good, Fetched done)>;
class History final : public Files {
  public:
	History(std::shared_ptr<kronuz::journal::IO> io, Limits limits, Fetch fetch = {});
	History(const History &) = delete;
	History &operator=(const History &) = delete;
	// Recovers the layout at `state`'s seal from the durable table and the chained seal
	// files; missing outputs are rebuilt, and a missing or rotted file is fetched from a
	// peer. Returns restored, or restoring() until the peer's files land; throws when
	// a file is lost.
	void recover(const State &state);
	// Adopts a source's history at `state`'s seal (an install): fetches the table and the
	// files it reaches, and the seal files above it and their outputs, all verified.
	void install(const State &state);
	// Queues the seal at `sealed`'s index: its seal file, its outputs, a table at a period seal.
	void seal(const State &sealed);
	// Drops a restore, or a layout the node never adopted; seals wait for the next restore.
	void abandon() noexcept;
	// The next queued write as an IO operation, while none is in flight.
	std::shared_ptr<kronuz::journal::IOOperation> take_io_operation();
	// Settles a finished write: its seals become durable, or history stalls on its error.
	void settle();
	// The layout reaches every seal through this index and its files are synced.
	bool durable_through(std::uint64_t index) const noexcept { return ready_ && durable_ >= index; }
	bool ready() const noexcept { return ready_; }
	bool restoring() const noexcept { return target_.has_value(); }
	// Writes queued or in flight.
	std::size_t queued() const noexcept { return writes_.size(); }
	// Why the last restore or write failed; history is not ready after one.
	const std::exception_ptr &failure() const noexcept { return failure_; }
	const Layout &layout() const noexcept { return layout_; }
	Directory &directory() noexcept { return directory_; }
	const Table &durable_table() const noexcept { return table_; }
	std::string read(std::string_view name) override;

  private:
	struct Wanted {
		std::vector<std::pair<std::string, Check>> names;
	};
	struct Queued {
		std::string bytes;
		unsigned writes = 0;
	};
	std::optional<std::string> find(std::string_view name);
	Check check(const std::optional<Digest> &digest) const;
	std::string verified(std::string_view name, const std::optional<Digest> &digest);
	std::optional<Table> local_table();
	std::optional<Table> peer_table();
	void begin(const State &state, bool install);
	void attempt();
	void restore();
	void adopt(Table table, const State &state);
	void chain(const State &state, Table &durable);
	void kept(const Step &step);
	void complete(std::uint64_t durable);
	void stage(Group group);
	void queue(Write write);
	void release(const Write &write) noexcept;
	void stall(std::exception_ptr error) noexcept;
	Directory directory_;
	Limits limits_;
	Fetch fetch_;
	Layout layout_;
	Table table_; // The durable table: the newest one a settled write published.
	std::uint64_t durable_ = 0, epoch_ = 0;
	bool ready_ = false, install_ = false, attempting_ = false;
	std::optional<State> target_; // The state a restore reaches.
	std::map<std::string, Reply, std::less<>> fetched_;
	std::size_t outstanding_ = 0;
	Write plan_; // A restore's writes, queued once it completes.
	std::map<std::string, std::string, std::less<>> planned_;
	std::vector<std::string> above_; // Seal files above the newest table, and their outputs.
	std::optional<std::size_t> fetched_group_; // The plan's group of fetched files.
	std::map<std::string, Queued, std::less<>> unsynced_;
	std::deque<std::shared_ptr<const Write>> writes_;
	std::shared_ptr<kronuz::journal::IOOperation> operation_;
	std::exception_ptr failure_;
	std::shared_ptr<History *> self_ = std::make_shared<History *>(this);
};
} // namespace detent::authority::history
