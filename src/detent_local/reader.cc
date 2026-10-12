#include "detent_local/reader.hh"
#include "resources.hh"
#include "state_equivalence.hh"
#include <stdexcept>

namespace detent::local {
GenerationReader::GenerationReader(std::filesystem::path directory, ViewLimits limits,
								   std::optional<distribution::Cursor> identity)
	: directory_(std::move(directory)), limits_(std::move(limits)), identity_(identity) {}

bool GenerationReader::refresh() {
	// Serialize refreshes so an older recovery cannot overwrite a newer install.
	std::lock_guard refresh_lock(refresh_mutex_);
	// Selected images are immutable and the installed root retains its lease.
	// An unchanged trusted selection needs no second mapping or image scan.
	auto selection = read_generation_selection(directory_);
	{
		std::lock_guard install_lock(installed_mutex_);
		if (directory_identity_ && *directory_identity_ != selection.directory_identity)
			throw std::invalid_argument("local generation directory was replaced");
		if (descriptor_ && *descriptor_ == selection.descriptor)
			return false;
	}
	auto recovered = recover_generation(directory_, limits_, {}, recovered_ ? &*recovered_ : nullptr);
	const auto &next = recovered.descriptor();
	if (identity_ && !identity_->same_identity(next.cursor))
		throw std::invalid_argument("local generation has an unexpected authority identity");
	if (recovered_ && recovered_->descriptor().cursor == next.cursor) {
		detail::StateEquivalence proof(*recovered_, recovered, resources::domain(limits_).storage_account());
		while (!proof.done()) {
			ValidationBudget budget;
			proof.step(budget);
		}
		if (!proof.equal())
			throw std::invalid_argument("local generation conflicts with the installed cursor");
	}
	ReadView retired;
	{
		std::lock_guard install_lock(installed_mutex_);
		if (directory_identity_ && *directory_identity_ != recovered.directory_identity())
			throw std::invalid_argument("local generation directory was replaced");
		if (descriptor_) {
			if (!descriptor_->cursor.same_identity(next.cursor) ||
				next.cursor.revision < descriptor_->cursor.revision)
				throw std::invalid_argument("local generation identity changed or revision regressed");
			if (next == *descriptor_)
				return false;
		}
		retired = std::move(installed_);
		installed_ = recovered.view();
		descriptor_ = next;
		directory_identity_ = recovered.directory_identity();
	}
	recovered_.emplace(std::move(recovered));
	// Root retirement never occurs while holding the SDK acquisition lock.
	return true;
}

ReadView GenerationReader::read_view() const {
	std::lock_guard lock(installed_mutex_);
	return installed_;
}

std::optional<Descriptor> GenerationReader::descriptor() const {
	std::lock_guard lock(installed_mutex_);
	return descriptor_;
}
} // namespace detent::local
