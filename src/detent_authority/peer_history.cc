#include "peer_history.hh"
#include <stdexcept>

namespace detent::authority {
kronuz::consensus::transport::FetchReader history_reader(std::shared_ptr<kronuz::journal::IO> io, std::size_t bytes) {
	if (!io || !bytes)
		throw std::invalid_argument("history reader");
	return [io = std::move(io), bytes](std::string_view name) { return history::serve(*io, name, bytes); };
}

PeerHistory::PeerHistory(asio::any_io_executor executor, std::vector<kronuz::consensus::NodeId> voters,
						 std::size_t slots, std::chrono::milliseconds retry)
	: executor_(executor), voters_(std::move(voters)), slots_(slots), retry_(retry), timer_(executor) {
	if (voters_.empty() || !slots_ || retry_ <= std::chrono::milliseconds::zero())
		throw std::invalid_argument("peer history bounds");
}
void PeerHistory::attach(std::weak_ptr<AuthorityPeers> peers) {
	peers_ = std::move(peers);
	pump();
}
void PeerHistory::stop() noexcept {
	stopped_ = true;
	queue_.clear();
	waiting_.clear();
	timer_.cancel();
}
history::Fetch PeerHistory::fetch() {
	return [self = shared_from_this()](const std::string &name, history::Check good, history::Fetched done) {
		self->want(name, std::move(good), std::move(done));
	};
}
void PeerHistory::want(std::string name, history::Check good, history::Fetched done) {
	if (stopped_)
		return;
	queue_.push_back({std::move(name), std::move(good), std::move(done), turn_++ % voters_.size()});
	pump();
}
// Issues each queued round's next ask while its voter has a free slot, oldest first.
void PeerHistory::pump() {
	auto peers = peers_.lock();
	if (stopped_ || !peers)
		return;
	for (auto it = queue_.begin(); it != queue_.end();) {
		const auto voter = voters_[(it->first + it->asked) % voters_.size()];
		if (busy_[voter] >= slots_) {
			++it;
			continue;
		}
		auto wanted = std::move(*it);
		it = queue_.erase(it);
		++busy_[voter];
		auto name = wanted.name;
		peers->fetch(voter, std::move(name),
					 [self = shared_from_this(), voter, wanted = std::move(wanted)](
						 std::error_code error, std::optional<std::string> bytes) mutable {
						 --self->busy_[voter];
						 self->answered(std::move(wanted), voter, error, std::move(bytes));
					 });
	}
}
void PeerHistory::answered(Wanted wanted, kronuz::consensus::NodeId voter, std::error_code error,
						   std::optional<std::string> bytes) {
	if (stopped_)
		return;
	if (!error && bytes && wanted.good(*bytes)) {
		wanted.done({std::move(bytes), {}});
		return pump();
	}
	// A failed reply or one above the bound fails again; anything else may pass.
	const bool lasting = !error || error == std::errc::io_error || error == std::errc::message_size;
	wanted.transient |= !lasting;
	wanted.why += (wanted.why.empty() ? "voter " : ", voter ") + std::to_string(voter) + " " +
				  (error ? error.message() : bytes ? "rotten" : "none");
	if (++wanted.asked < voters_.size()) {
		queue_.push_back(std::move(wanted));
	} else if (!wanted.transient) {
		wanted.done({std::nullopt, std::move(wanted.why)});
	} else {
		wanted.first = turn_++ % voters_.size();
		wanted.asked = 0;
		wanted.transient = false;
		wanted.why.clear();
		waiting_.push_back(std::move(wanted));
		if (!timing_) {
			timing_ = true;
			timer_.expires_after(retry_);
			timer_.async_wait([weak = weak_from_this()](const asio::error_code &) {
				auto self = weak.lock();
				if (!self || self->stopped_)
					return;
				self->timing_ = false;
				for (auto &waiting : self->waiting_)
					self->queue_.push_back(std::move(waiting));
				self->waiting_.clear();
				self->pump();
			});
		}
	}
	pump();
}
} // namespace detent::authority
