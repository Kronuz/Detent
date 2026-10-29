#include "applied_delta_fixture.hh"
#include "paged_delta_plan.hh"
#include "paged_dependency_check.hh"
#include "paged_disk_fixture.hh"
#include "paged_publication_artifacts.hh"
#include "reader_capsule.hh"
#include <completion/native_completion.h>
template <class Error, class Call> void rejects(Call call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid paged artifact operation accepted");
}
void test_encoding(l::ReaderMetadataEncoding encoding) {
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
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image, storage);
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
					l::detail::PagedPlanBuilder construction(recovered, delta, next, domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				} else {
					l::detail::PagedDeltaPlanBuilder construction(
						recovered, parse_delta(false, 2, "app", account), next, domain);
					while (!construction.done()) {
						l::ValidationBudget budget;
						construction.step(budget);
					}
					plan.emplace(construction.take());
				}
				auto artifacts = domain.factory().make<l::detail::PagedPublicationArtifacts>(account);
				if (pressure) {
					auto admission = account->admit(account->limit() - account->used());
					rejects<d::StoragePressure>([&] { artifacts->populate(*plan, encoding); });
					rejects<std::logic_error>([&] { artifacts->artifacts(); });
					require(plan->descriptor().generation == next);
				} else {
					artifacts->populate(*plan, encoding);
					const auto rows = artifacts->artifacts();
					require(rows.size() == (progress ? 4 : 6));
					require(artifacts->reused().size() == (progress ? 1 : 0));
					const auto &lease = rows[rows.size() - 4];
					const auto ownership =
						l::decode_generation_ownership({lease.bytes().data(), lease.bytes().size()});
					require(ownership.layout_version == 2 && ownership.storage == storage &&
							ownership.generation == next);
					std::vector<std::uint64_t> lengths;
					for (const auto &segment : plan->segments())
						lengths.push_back(segment.bytes->size());
					require(artifacts->usage() == l::DiskLedger::paged_peak(lengths, encoding));
					const auto &record = rows[rows.size() - 2];
					const auto metadata =
					    encoding == l::ReaderMetadataEncoding::Capsule
					        ? l::detail::decode_reader_capsule({record.bytes().data(), record.bytes().size()}).metadata
					        : l::detail::decode_reader_metadata({record.bytes().data(), record.bytes().size()});
					require(record.bytes().size() == l::reader_metadata_length(encoding));
					const auto legacy_usage = l::DiskLedger::paged_peak(lengths);
					require(artifacts->usage().bytes - legacy_usage.bytes ==
					        (encoding == l::ReaderMetadataEncoding::Capsule ? 432 : 0));
					require(metadata.storage == storage && metadata.generation == next &&
					        metadata.root_checksum == plan->descriptor().checksum &&
					        metadata.counts.bools == plan->counts().bools &&
					        metadata.counts.ints == plan->counts().ints &&
					        metadata.physical_bytes == artifacts->target_physical_bytes());
					require(artifacts->prefix_usage(0) == l::DiskUsage{});
					require(artifacts->prefix_usage(rows.size()) == artifacts->usage());
					require(artifacts->prefix_usage(rows.size() - 2).generations == 1);
					rejects<std::out_of_range>([&] { artifacts->prefix_usage(rows.size() + 1); });
					artifacts->attach(std::move(*plan));
					rejects<std::logic_error>([&] { plan->root(); });
					if (progress) {
						auto session =
							l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
						const auto injected = std::make_exception_ptr(
							std::runtime_error("dependency read failed during cancellation"));
						bool fired = false, closed = false;
						{
							l::detail::PagedDependencyCheck check(domain.factory(), session, artifacts,
																  ::getuid());
							while (!check.done()) {
								const auto &request = check.request();
								check.submitted();
								kronuz::io::completion::Result result;
								if (!fired && request.kind == kronuz::io::completion::Kind::Read) {
									check.cancel();
									result.token = request.token;
									result.error = injected;
									fired = true;
								} else {
									result = kronuz::journal::detail::execute_primitive(check.io(), request);
									closed |=
										fired && request.kind == kronuz::io::completion::Kind::CloseFile;
								}
								require(check.complete(std::move(result)));
							}
							require(fired && closed && check.error() == injected && !check.in_flight());
						}
						session->close();
					}
				}
			}
			domain.close().wait();
			require(account->used() == initial);
		}
	}
}
int main() {
	test_encoding(l::ReaderMetadataEncoding::Legacy);
	test_encoding(l::ReaderMetadataEncoding::Capsule);
}
