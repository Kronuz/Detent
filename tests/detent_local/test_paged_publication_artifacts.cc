#include "applied_delta_fixture.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include "paged_publication_artifacts.hh"
#include "reader_metadata.hh"
#include "live_record.hh"
template <class Error, class Call> void rejects(Call call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged artifact operation accepted");
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-artifacts-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::GenerationId generation{}, next{};
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder builder(source, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image);
	auto recovered = l::recover_generation(directory);
	for (bool progress : {false, true}) {
		for (bool pressure : {false, true}) {
			auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
			auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
			d::RetirementRuntime runtime(baseline, 1);
			auto domain = runtime.domain(account, 8);
			const auto initial = account->used();
			{
				std::optional<l::detail::PagedPlan> plan;
				if (progress) {
					auto delta =
						std::make_shared<const d::ValidatedDelta>(parse_delta(true, 2, "app", account));
					l::detail::PagedPlanBuilder construction(recovered, delta, domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				} else {
					l::detail::PagedDeltaPlanBuilder construction(recovered, parse_delta(false, 2, "app", account), domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				}
				next = l::next_generation(recovered.descriptor(), plan->descriptor().cursor, plan->descriptor().profile,
										  false)
						   .id;
				auto artifacts = domain.factory().make<l::detail::PagedPublicationArtifacts>(account);
				if (pressure) {
					auto admission = account->admit(account->limit() - account->used());
					rejects<d::StoragePressure>([&] { artifacts->populate(*plan); });
					rejects<std::logic_error>([&] { artifacts->artifacts(); });
					require(plan->descriptor().generation == next);
				} else {
					artifacts->populate(*plan);
					const auto rows = artifacts->artifacts();
					require(artifacts->reused().size() == (progress ? 1 : 0));
					// Owner records, packs, then the image, `.live` and `.read`: the agent store's files.
					const auto segments = (rows.size() - 3) / 2;
					require(plan->segments().size() == segments && rows.size() == 2 * segments + 3);
					for (std::size_t i = 0; i < segments; ++i) {
						require(rows[i].filename().ends_with(".owner"));
						require(rows[segments + i].filename().ends_with(".pack"));
					}
					const auto &image_row = rows[2 * segments], &live_row = rows[2 * segments + 1], &read_row = rows.back();
					require(image_row.filename().ends_with(".image") && live_row.filename().ends_with(".live") &&
							read_row.filename().ends_with(".read"));
					require(artifacts->usage().entries == rows.size() && artifacts->usage().generations == 1);
					const auto metadata =
					    l::detail::decode_reader_metadata({read_row.bytes().data(), read_row.bytes().size()});
					require(metadata.generation == next &&
					        metadata.root_checksum == plan->descriptor().checksum &&
					        metadata.counts.records == plan->counts().records &&
					        metadata.physical_bytes == artifacts->target_physical_bytes());
					// `.live` binds the generation, its root and the whole `.read`, and rows every segment it reaches.
					const auto live = l::detail::decode_live_record({live_row.bytes().data(), live_row.bytes().size()}, 1024);
					require(live.generation == next && live.root_checksum == plan->descriptor().checksum &&
							live.read_digest == l::detail::reader_metadata_digest({read_row.bytes().data(), read_row.bytes().size()}) &&
							live.rows.size() == segments + artifacts->reused().size());
					artifacts->attach(std::move(*plan));
					rejects<std::logic_error>([&] { plan->root(); });
				}
			}
			domain.close().wait();
			require(account->used() == initial);
		}
	}
}
