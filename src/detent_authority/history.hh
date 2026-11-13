#pragma once
// A voter's history directory (namespace-delivery D6 "Commit", "Recovery"): seal
// files committed in seal order, roll-up outputs in one sync group, the `layout`
// table at period seals, and a collector that checks against the durable table.
#include "history_layout.hh"
#include <completion/io.h>
#include <functional>
#include <memory>
#include <set>

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

  private:
	std::shared_ptr<kronuz::journal::IO> io_;
	Limits limits_;
};
// Fetches a history file by name from a peer, which holds the same bytes (D6 "Recovery").
using Fetch = std::function<std::optional<std::string>(std::string_view name)>;
class History {
  public:
	History(std::shared_ptr<kronuz::journal::IO> io, Limits limits, Fetch fetch = {});
	// Recovers the layout at `state`'s seal from the durable table and the chained seal
	// files; missing outputs are rebuilt, and a missing or rotted file is fetched from a peer.
	void recover(const State &state);
	// Adopts a source's history at `state`'s seal (an install): fetches the table and the
	// files it reaches, and the seal files above it and their outputs, all verified.
	void install(const State &state);
	// Commits the seal at `sealed`'s index: its seal file, its outputs, a table at a period seal.
	void seal(const State &sealed);
	// The layout reaches every seal through this index and its seal files are durable.
	bool durable_through(std::uint64_t index) const noexcept { return ready_ && layout_.table().index >= index; }
	bool ready() const noexcept { return ready_; }
	const Layout &layout() const noexcept { return layout_; }
	Directory &directory() noexcept { return directory_; }
	const Table &durable_table() const noexcept { return durable_; }

  private:
	std::string verified(std::string_view name, const std::optional<Digest> &digest);
	void adopt(Table table, const State &state);
	void chain(const State &state);
	void collect();
	Directory directory_;
	Limits limits_;
	Fetch fetch_;
	Layout layout_;
	Table durable_;
	bool ready_ = false;
};
} // namespace detent::authority::history
