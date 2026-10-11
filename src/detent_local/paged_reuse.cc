#include "paged_reuse.hh"
namespace detent::local::detail {
namespace p = paged;
namespace d = distribution;
ReuseNavigator PagedPlanBuilder::reuse(const RecoveredGeneration &base,
									   std::shared_ptr<d::StorageLedger> account, std::string_view key,
									   std::size_t depth) {
	if (!base.paged_)
		throw std::invalid_argument("reuse requires an actually recovered paged root");
	base.paged_->root();
	return ReuseNavigator(std::move(account), base.paged_, key, depth);
}
ReuseNavigator PagedPlanBuilder::reuse_at(const RecoveredGeneration &base,
										  std::shared_ptr<d::StorageLedger> account, p::Reference reference,
										  std::string_view entering) {
	if (!base.paged_)
		throw std::invalid_argument("reuse requires an actually recovered paged root");
	base.paged_->root();
	return ReuseNavigator(std::move(account), base.paged_, reference, entering);
}
} // namespace detent::local::detail
