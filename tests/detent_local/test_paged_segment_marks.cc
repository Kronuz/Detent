#include "paged_disk_fixture.hh"
#include "paged_segment_marks.hh"
int main() {
	const auto root =
		std::filesystem::current_path() / ".scratch" / ("paged-marks-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{root};
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	auto account = std::make_shared<d::StorageLedger>(32 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 32);
	const auto initial = account->used();
	l::ViewLimits limits;
	limits.retirement_domain = &domain;
	limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto capture = [&] {
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		install(root, builder.take(), storage);
		auto actual = l::recover_generation(root, limits);
		auto certificate = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		certificate->capture(actual);
		return certificate;
	};
	{
		auto first = capture();
		generation.fill('n');
		auto second = capture();
		require(first->dependencies().size() == 1 && second->dependencies().size() == 1);
		require(first->dependencies()[0].segment != second->dependencies()[0].segment);
		auto marks =
			domain.make<l::detail::PagedSegmentMarks>(account, first->directory_identity(), storage, 2);
		auto insert = [&](auto certificate) {
			marks->begin(certificate);
			l::ValidationBudget zero{0, 0};
			marks->step(zero);
			require(marks->busy());
			while (marks->busy()) {
				l::ValidationBudget budget{1, 1};
				marks->step(budget);
			}
		};
		insert(first);
		insert(first);
		insert(second);
		require(marks->size() == 2);
		bool rejected = false;
		try {
			marks->query(storage);
		} catch (const std::logic_error &) {
			rejected = true;
		}
		require(rejected);
		marks->seal();
		auto contains = [&](l::StorageIdentity id) {
			marks->query(id);
			l::ValidationBudget zero{0, 0};
			require(!marks->query_step(zero));
			std::optional<bool> result;
			for (unsigned turns = 0; !result; ++turns) {
				require(turns < 10000);
				l::ValidationBudget budget{1, 1};
				result = marks->query_step(budget);
			}
			return *result;
		};
		require(contains(first->dependencies()[0].segment));
		require(contains(second->dependencies()[0].segment));
		require(!contains(storage));
		auto limited =
			domain.make<l::detail::PagedSegmentMarks>(account, first->directory_identity(), storage, 1);
		limited->begin(first);
		while (limited->busy()) {
			l::ValidationBudget budget{1, 1};
			limited->step(budget);
		}
		limited->begin(second);
		rejected = false;
		try {
			while (limited->busy()) {
				l::ValidationBudget budget{1, 1};
				limited->step(budget);
			}
		} catch (const std::length_error &) {
			rejected = true;
		}
		require(rejected && limited->size() == 1);
		rejected = false;
		try {
			limited->seal();
		} catch (const std::logic_error &) {
			rejected = true;
		}
		require(rejected);
	}
	domain.close().wait();
	require(account->used() == initial && limits.mapping_account->used() == 0);
}
