#include "applied_delta_fixture.hh"
#include "mapped_recovery_admission.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "recovery_context.hh"
#include "store_core.hh"
#include <chrono>
#include <thread>
struct Resident final : d::RetirementNode {};
template <class Error, class F> void rejects(F action) {
	try {
		action();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid mapped admission accepted");
}
template <class F> void wait_for(F predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!predicate()) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("mapped-admission-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
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
	install(directory, image, storage);
	auto heap = std::make_shared<d::StorageLedger>(32 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
	d::RetirementRuntime runtime(heap, 2);
	auto domain = runtime.domain(heap, 256);
	{
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		auto recovered = l::recover_generation(directory, limits);
		l::detail::StoreCore core(heap, {}, domain.factory());
		core.retain_recovered(recovered);
		for (bool progress : {false, true}) {
			std::optional<l::detail::PagedPlan> plan;
			if (progress) {
				auto delta = std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", heap));
				l::detail::PagedPlanBuilder builder(recovered, delta, next, domain);
				while (!builder.done()) {
					l::ValidationBudget budget;
					builder.step(budget);
				}
				plan.emplace(builder.take());
			} else {
				l::detail::PagedDeltaPlanBuilder builder(recovered, parse_delta(false, 2, "app", heap), next,
														 domain);
				while (!builder.done()) {
					l::ValidationBudget budget;
					builder.step(budget);
				}
				plan.emplace(builder.take());
			}
			{
				auto other_heap = std::make_shared<d::StorageLedger>(1024 * 1024);
				rejects<std::invalid_argument>([&] {
					auto mismatch = domain.make<l::detail::MappedRecoveryAdmission>(other_heap, mappings,
																					domain.factory());
				});
			}
			{
				auto other_mappings = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
				auto mismatch =
					domain.make<l::detail::MappedRecoveryAdmission>(heap, other_mappings, domain.factory());
				mismatch->populate(*plan, core.verified_paged);
				require(!mismatch->warm());
				mismatch.reset();
				wait_for([&] { return other_mappings->used() == 0; });
			}
			{
				const auto baseline = mappings->used();
				auto limited = runtime.domain(heap, 1);
				auto unavailable =
					limited.make<l::detail::MappedRecoveryAdmission>(heap, mappings, limited.factory());
				rejects<d::StoragePressure>([&] { unavailable->populate(*plan, core.verified_paged); });
				require(mappings->used() == baseline && plan->descriptor().generation == next);
				unavailable.reset();
				limited.close().wait();
			}
			for (bool warm : {false, true}) {
				const auto baseline = mappings->used();
				std::size_t required_residents = 5 + plan->segments().size();
				if (!warm || !progress)
					++required_residents;
				if (warm && !progress)
					++required_residents;
				if (!warm)
					for (std::size_t i = 0; i < plan->registry()->inputs().size(); ++i)
						if (plan->base_reachability(i).pages)
							++required_residents;
				for (std::size_t capacity = 1; capacity < required_residents; ++capacity) {
					auto limited = runtime.domain(heap, capacity);
					auto insufficient =
						limited.make<l::detail::MappedRecoveryAdmission>(heap, mappings, limited.factory());
					rejects<d::StoragePressure>([&] {
						insufficient->populate(*plan,
											   warm ? core.verified_paged
													: d::RetirementHandle<l::detail::LeasedPagedRoot>{});
					});
					require(plan->descriptor().generation == next);
					insufficient.reset();
					limited.close().wait();
					require(mappings->used() == baseline);
				}
				std::uint64_t needed = l::detail::rounded_mapping_bytes(plan->descriptor().length);
				for (const auto &segment : plan->segments())
					needed += l::detail::rounded_mapping_bytes(segment.ownership.length);
				if (!warm)
					for (std::size_t i = 0; i < plan->registry()->inputs().size(); ++i)
						if (plan->base_reachability(i).pages)
							needed += l::detail::rounded_mapping_bytes(
								plan->registry()->inputs()[i].ownership.length);
				for (bool short_capacity : {true, false}) {
					auto pressure = mappings->admit(mappings->limit() - baseline - needed + short_capacity);
					auto admission =
						domain.make<l::detail::MappedRecoveryAdmission>(heap, mappings, domain.factory());
					if (short_capacity) {
						rejects<d::StoragePressure>([&] {
							admission->populate(*plan,
												warm ? core.verified_paged
													 : d::RetirementHandle<l::detail::LeasedPagedRoot>{});
						});
						rejects<std::logic_error>([&] { admission->warm(); });
					} else {
						admission->populate(*plan, warm ? core.verified_paged
														: d::RetirementHandle<l::detail::LeasedPagedRoot>{});
						require(admission->warm() == warm && admission->reserved_bytes() == needed);
						require(mappings->used() == mappings->limit());
						admission->bind(plan->descriptor(), plan->directory_identity(),
										plan->storage_identity());
						auto wrong = plan->descriptor();
						++wrong.cursor.revision;
						rejects<std::invalid_argument>([&] {
							admission->bind(wrong, plan->directory_identity(), plan->storage_identity());
						});
						auto root = admission->take_root();
						rejects<std::logic_error>([&] { root->bytes(); });
						rejects<std::logic_error>([&] { admission->take_root(); });
					}
					require(plan->descriptor().generation == next);
					require(l::read_generation_selection(directory).descriptor == recovered.descriptor());
					admission.reset();
					wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
				}
			}
			for (bool warm : {false, true}) {
				const auto baseline = mappings->used();
				auto admission =
					domain.make<l::detail::MappedRecoveryAdmission>(heap, mappings, domain.factory());
				admission->populate(*plan, warm ? core.verified_paged
												: d::RetirementHandle<l::detail::LeasedPagedRoot>{});
				rejects<std::logic_error>([&] { admission->take_view(); });
				rejects<std::logic_error>([&] { admission->capability()->root(); });
				{
					l::detail::RecoveryContext wrong_selection{
						domain.factory(),
						heap,
						mappings,
						limits,
						warm ? core.verified_paged : d::RetirementHandle<l::detail::LeasedPagedRoot>{},
						admission};
					rejects<std::invalid_argument>(
						[&] { l::detail::RecoveryEngine::recover(directory, wrong_selection); });
					if (warm) {
						wrong_selection.prior.reset();
						rejects<std::invalid_argument>([&] {
							l::detail::RecoveryEngine::recover(directory / "missing", wrong_selection);
						});
					}
				}
				std::vector<d::RetirementHandle<Resident>> residents;
				for (;;) {
					try {
						residents.push_back(domain.make<Resident>());
					} catch (const d::StoragePressure &) {
						break;
					}
				}
				auto pressure = mappings->admit(mappings->limit() - mappings->used());
				const auto name = l::generation_name(next);
				write(directory / (name + ".image"), {plan->bytes()->data(), plan->bytes()->size()});
				write(directory / (name + ".lease"),
					  l::encode_generation_ownership(
						  {storage, next, plan->descriptor().length, plan->descriptor().checksum, 2}));
				for (const auto &segment : plan->segments()) {
					const l::detail::SegmentNames names(segment.ownership.segment);
					write(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
					write(directory / names.payload_name(), {segment.bytes->data(), segment.bytes->size()});
				}
				write(directory / "selected", l::encode_descriptor(plan->descriptor()));
				{
					l::detail::RecoveryContext context{
						domain.factory(),
						heap,
						mappings,
						limits,
						warm ? core.verified_paged : d::RetirementHandle<l::detail::LeasedPagedRoot>{},
						admission};
					context.budget = {1, 1};
					auto current = l::detail::RecoveryEngine::recover(directory, std::move(context));
					require(current.descriptor() == plan->descriptor());
					require(current.view().size() == plan->root().records);
					require(current.view().contains("app", "count") &&
							current.view().contains("app", "flag"));
					require(!current.view().contains("app", "missing"));
					require(current.view().get("app", "count").value() ==
							(progress ? std::numeric_limits<std::int64_t>::min() : 1));
					for (const auto hash : {"host-a", "host-b", "host-c"})
						for (const auto switch_value : {"beta", "production"})
							require(current.view().eval("app", "flag", hash, switch_value).value() ==
									(progress
										 ? recovered.view().eval("app", "flag", hash, switch_value).value()
										 : true));
					require(mappings->used() == mappings->limit());
				}
				write(directory / "selected", l::encode_descriptor(recovered.descriptor()));
				admission.reset();
				wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
			}
		}
	}
	l::detail::RecoveryContext context{domain.factory(), heap, mappings, {}, {}, {}};
	context.limits.retirement_domain = &domain;
	auto moved = std::move(domain);
	{
		auto recovered = l::detail::RecoveryEngine::recover(directory, std::move(context));
		require(recovered.view().size() == image.root().records);
	}
	moved.close().wait();
	require(mappings->used() == 0);
}
