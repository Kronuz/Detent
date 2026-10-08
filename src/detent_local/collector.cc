#include "detent_local/collector.hh"

namespace detent::local {
namespace c = kronuz::io::completion;
CollectorControls::CollectorControls(std::shared_ptr<StorageSession> session) : session_(std::move(session)) {
	if (!session_ || session_->closed() || !session_->backend()->managed_capabilities().complete())
		throw std::invalid_argument("collector requires a complete managed backend");
	for (auto &lease : leases_) {
		lease = session_->backend()->make_closed_file();
		if (!lease)
			throw std::runtime_error("collector lease control missing");
	}
	scratch_ = session_->backend()->make_closed_file();
	cursor_ = session_->backend()->make_closed_cursor();
	if (!scratch_ || !cursor_)
		throw std::runtime_error("collector scratch or cursor control missing");
}
c::File &CollectorControls::lease(std::size_t slot) const {
	if (slot >= leases_.size())
		throw std::out_of_range("collector lease slot exceeds fixed capacity");
	return *leases_[slot];
}
CollectorControls::Claim CollectorControls::claim(CollectorUse use) {
	if (session_->closed())
		throw std::logic_error("collector session closed");
	if (use != CollectorUse::Census && use != CollectorUse::Reclaim)
		throw std::invalid_argument("unsupported collector use");
	if (claimed_)
		throw std::logic_error("collector controls already active");
	if (serial_ == std::numeric_limits<std::uint64_t>::max())
		throw std::overflow_error("collector claim identity exhausted");
	use_ = use;
	claimed_ = true;
	return {++serial_, use};
}
void CollectorControls::release(Claim claim) {
	if (!claimed_ || !claim.serial || claim.serial != serial_ || claim.use != use_)
		throw std::logic_error("stale collector claim");
	claimed_ = false;
}
std::shared_ptr<CollectorControls> make_collector_controls(std::shared_ptr<StorageSession> session) {
	if (!session)
		throw std::invalid_argument("collector session missing");
	return std::allocate_shared<CollectorControls>(
		c::OwnedAllocator<CollectorControls>(session->allocation_context()), session);
}
} // namespace detent::local
