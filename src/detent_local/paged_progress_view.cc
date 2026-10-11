#include "paged_progress_view.hh"
#include "read_root.hh"
namespace detent::local::detail {
ReadView PagedProgressView::take(distribution::RetirementDomain &domain,
								 distribution::RetirementHandle<LeasedPagedRoot> capability) {
	if (!capability)
		throw std::invalid_argument("missing leased paged progress root");
	const auto root = capability->root();
	const auto counts = capability->counts();
	const auto inputs = capability->registry()->inputs();
	using Owner = distribution::RetirementHandle<LeasedPagedRoot>;
	auto owner = std::allocate_shared<Owner>(distribution::StorageAllocator<Owner>(domain.storage_account()),
											 std::move(capability));
	return ReadView(domain.make<ReadView::Root>(owner, PagedReadState{root, inputs}, counts.bools,
												counts.ints, counts.overrides, counts.strings));
}
} // namespace detent::local::detail
