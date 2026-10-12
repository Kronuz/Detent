#include "paged_certificate.hh"
namespace detent::local::detail {
struct PagedDependencyCertificate::Dependencies final : distribution::RetirementNode {
	std::shared_ptr<const void> identity;
	std::vector<SegmentOwnership, distribution::StorageAllocator<SegmentOwnership>> rows;
	explicit Dependencies(std::shared_ptr<distribution::StorageLedger> account)
		: rows(distribution::StorageAllocator<SegmentOwnership>(account)) {}
	bool retire_step() noexcept override {
		for (unsigned i = 0; i < 16 && !rows.empty(); ++i)
			rows.pop_back();
		return rows.empty();
	}
};
PagedDependencyCertificate::PagedDependencyCertificate(std::shared_ptr<distribution::StorageLedger> account,
													   distribution::RetirementFactory factory)
	: account_(std::move(account)), factory_(std::move(factory)) {}
void PagedDependencyCertificate::capture(const RecoveredGeneration &generation,
										 const PagedDependencyCertificate *prior) {
	if (sealed_ || failed_ || dependencies_)
		throw std::logic_error("certificate capture unavailable");
	try {
		if (!generation.paged_)
			throw std::invalid_argument("certificate requires actual paged recovery");
		const auto &source = *generation.paged_;
		descriptor_ = source.descriptor();
		root_ = source.root();
		directory_ = source.directory_identity();
		storage_ = source.storage_identity();
		const auto registry = source.registry();
		if (prior && prior->sealed_ && prior->account_ == account_ && prior->storage_ == storage_ &&
			prior->directory_ == directory_ &&
			prior->dependencies_->identity == registry->metadata_identity()) {
			dependencies_ = prior->dependencies_;
		} else {
			auto rows = factory_.make<Dependencies>(account_);
			rows->identity = registry->metadata_identity();
			rows->rows.reserve(registry->inputs().size());
			dependencies_ = rows;
			for (const auto &input : registry->inputs())
				rows->rows.push_back(input.ownership);
		}
		if (dependencies_->rows.size() != root_.segments)
			throw std::invalid_argument("certificate dependency count mismatch");
		sealed_ = true;
	} catch (...) {
		failed_ = true;
		throw;
	}
}
const Descriptor &PagedDependencyCertificate::descriptor() const {
	if (!sealed_)
		throw std::logic_error("unsealed dependency certificate");
	return descriptor_;
}
const paged::Root &PagedDependencyCertificate::root() const {
	descriptor();
	return root_;
}
DirectoryIdentity PagedDependencyCertificate::directory_identity() const {
	descriptor();
	return directory_;
}
StorageIdentity PagedDependencyCertificate::storage_identity() const {
	descriptor();
	return storage_;
}
std::span<const SegmentOwnership> PagedDependencyCertificate::dependencies() const {
	descriptor();
	return dependencies_->rows;
}
bool PagedDependencyCertificate::retire_step() noexcept {
	dependencies_.reset();
	return true;
}
} // namespace detent::local::detail
