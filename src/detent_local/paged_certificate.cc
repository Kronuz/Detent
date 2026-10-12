#include "paged_certificate.hh"
#include "paged_named_root.hh"
#include "paged_root_prefix.hh"
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
	if (!generation.paged_)
		throw std::invalid_argument("certificate requires actual paged recovery");
	capture(*generation.paged_, prior);
}
void PagedDependencyCertificate::capture(const LeasedPagedRoot &source,
										 const PagedDependencyCertificate *prior) {
	if (sealed_ || failed_ || dependencies_)
		throw std::logic_error("certificate capture unavailable");
	try {
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
void PagedDependencyCertificate::begin_recovered(const LeasedPagedRoot &source) {
	if (sealed_ || failed_ || dependencies_ || capturing_)
		throw std::logic_error("certificate capture unavailable");
	try {
		descriptor_ = source.descriptor();
		root_ = source.root();
		directory_ = source.directory_identity();
		storage_ = source.storage_identity();
		capture_registry_ = source.registry();
		capture_inputs_ = capture_registry_->inputs();
		dependencies_ = factory_.make<Dependencies>(account_);
		dependencies_->identity = capture_registry_->metadata_identity();
		dependencies_->rows.reserve(capture_inputs_.size());
		capturing_ = true;
	} catch (...) {
		failed_ = true;
		throw;
	}
}
void PagedDependencyCertificate::begin_named(const PagedNamedRoot &named, const GraphValidator &graph) {
	if (sealed_ || failed_ || dependencies_ || capturing_)
		throw std::logic_error("certificate capture unavailable");
	try {
		root_ = graph.root();
		if (paged::encode_root(root_, paged_format_limits()) !=
			paged::encode_root(named.root(), paged_format_limits()))
			throw std::invalid_argument("named certificate graph mismatch");
		descriptor_ = named.descriptor();
		directory_ = named.directory_identity();
		storage_ = named.storage_identity();
		auto source = graph.validated_inputs();
		capture_owner_ = std::move(source.first);
		capture_inputs_ = source.second;
		dependencies_ = factory_.make<Dependencies>(account_);
		dependencies_->rows.reserve(capture_inputs_.size());
		capturing_ = true;
	} catch (...) {
		failed_ = true;
		throw;
	}
}
void PagedDependencyCertificate::capture_step(ValidationBudget &budget) {
	if (!capturing_ || failed_ || sealed_)
		throw std::logic_error("named certificate capture unavailable");
	try {
		while (capture_index_ < capture_inputs_.size() && budget.rows) {
			if (!capture_charge_)
				capture_charge_ = sizeof(SegmentOwnership);
			const auto count = std::min(capture_charge_, budget.bytes);
			capture_charge_ -= count;
			budget.bytes -= count;
			if (capture_charge_)
				return;
			--budget.rows;
			const auto &ownership = capture_inputs_[capture_index_++].ownership;
			if (ownership.storage != storage_)
				throw std::invalid_argument("named certificate storage mismatch");
			dependencies_->rows.push_back(ownership);
		}
		if (capture_index_ == capture_inputs_.size() && budget.rows) {
			--budget.rows;
			if (dependencies_->rows.size() != root_.segments)
				throw std::invalid_argument("named certificate dependency count mismatch");
			capture_inputs_ = {};
			capture_owner_.reset();
			capture_registry_.reset();
			capturing_ = false;
			sealed_ = true;
		}
	} catch (...) {
		failed_ = true;
		throw;
	}
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
	capture_inputs_ = {};
	capture_owner_.reset();
	capture_registry_.reset();
	dependencies_.reset();
	return true;
}
} // namespace detent::local::detail
