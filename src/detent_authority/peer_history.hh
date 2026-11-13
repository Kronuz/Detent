#pragma once
// History files between voters over consensus's fetch lane (D91, consensus
// docs/mechanisms/peer-fetch.md): each voter serves its history directory through
// `history_reader`, and `PeerHistory` is the `history::Fetch` its History asks.
#include "peers.hh" // first, as peers.hh says
#include "history.hh"
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace detent::authority {
// A voter's `PeerOptions::reader`: only names history writes, each file at most `bytes`
// (the lane's `fetch_bytes`), read on the owner executor (docs/status/namespace-s5.md).
kronuz::consensus::transport::FetchReader history_reader(std::shared_ptr<kronuz::journal::IO> io, std::size_t bytes);

// Asks the other voters for a name in turn, at most `slots` fetches awaiting each. The
// first bytes win; nothing when every voter answers that it holds none. A round with an
// error (no lane yet, a timeout) runs again after `retry`. Callbacks run on the owner.
class PeerHistory : public std::enable_shared_from_this<PeerHistory> {
  public:
	PeerHistory(asio::any_io_executor executor, std::vector<kronuz::consensus::NodeId> voters, std::size_t slots,
				std::chrono::milliseconds retry);
	// The transport, once it exists; a fetch waits for it.
	void attach(std::weak_ptr<AuthorityPeers> peers);
	// Drops every fetch without answering it; the owner is shutting down.
	void stop() noexcept;
	history::Fetch fetch();

  private:
	struct Wanted {
		std::string name;
		history::Fetched done;
		std::size_t next = 0, missing = 0;
	};
	void want(std::string name, history::Fetched done);
	void pump();
	void answered(Wanted wanted, std::error_code error, std::optional<std::string> bytes);
	asio::any_io_executor executor_;
	std::vector<kronuz::consensus::NodeId> voters_;
	std::size_t slots_;
	std::chrono::milliseconds retry_;
	std::weak_ptr<AuthorityPeers> peers_;
	std::deque<Wanted> queue_;
	std::vector<Wanted> waiting_; // Rounds that met an error, until the retry timer.
	std::map<kronuz::consensus::NodeId, std::size_t> busy_;
	asio::steady_timer timer_;
	bool timing_ = false, stopped_ = false;
};
} // namespace detent::authority
