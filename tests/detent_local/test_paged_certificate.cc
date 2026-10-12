#include "paged_certificate.hh"
#include "paged_plan.hh"
#include "paged_progress_fixture.hh"
#include <chrono>
#include <thread>
int main() {
	const auto root =
		std::filesystem::current_path() / ".scratch" / ("paged-certificate-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{root};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder snapshot(source, storage, generation);
	while (!snapshot.done()) {
		l::ValidationBudget budget;
		snapshot.step(budget);
	}
	auto image = snapshot.take();
	install(root, image, storage);
	auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 16);
	const auto initial = account->used();
	auto mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	{
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mapping_account;
		auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(root, limits));
		auto first = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		first->capture(*base);
		require(first->descriptor() == image.descriptor() && first->storage_identity() == storage);
		require(first->dependencies().size() == image.segments().size());
		{
			l::detail::PagedPlanBuilder builder(*base, progress(base->descriptor().cursor, account), next,
												domain);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			auto plan = builder.take();
			const auto name = l::generation_name(next);
			write(root / (name + ".lease"),
				  l::encode_generation_ownership(
					  {storage, next, plan.descriptor().length, plan.descriptor().checksum, 2}));
			write(root / (name + ".image"), {plan.bytes()->data(), plan.bytes()->size()});
			write(root / "selected", l::encode_descriptor(plan.descriptor()));
		}
		auto newer = std::optional<l::RecoveredGeneration>(l::recover_generation(root, limits, {}, &*base));
		auto second = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
		second->capture(*newer, first.get());
		require(first->dependencies().data() == second->dependencies().data());
		require(first->descriptor() != second->descriptor());
		first.reset();
		base.reset();
		newer.reset();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (mapping_account->used()) {
			require(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		require(second->dependencies().size() == image.segments().size());
		require(second->dependencies()[0] == image.segments()[0].ownership);
	}
	domain.close().wait();
	require(account->used() == initial && mapping_account->used() == 0);
}
