#include "paged_disk_fixture.hh"
#include "paged_segment_collection.hh"
#include "store_core.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <thread>
namespace c = kronuz::io::completion;
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("paged-segment-collection-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 256);
	for (unsigned variant = 0; variant < 14; ++variant) {
		std::filesystem::remove_all(directory);
		install(directory, image, storage);
		l::StorageIdentity orphan{};
		orphan.fill('z');
		const l::detail::SegmentNames names(orphan);
		const std::string bytes(257, 'x');
		const l::SegmentOwnership ownership{storage, orphan, bytes.size(),
											l::paged::physical_commitment(bytes)};
		write(directory / names.owner_name(), l::encode_segment_ownership(ownership));
		write(directory / names.payload_name(), bytes);
		if (variant == 9)
			require(std::filesystem::remove(directory / names.payload_name()));
		if (variant == 10) {
			l::GenerationId legacy{};
			legacy.fill('v');
			l::ImageBuilder flat(source, legacy);
			while (!flat.done()) {
				l::ValidationBudget budget;
				flat.step(budget);
			}
			write(directory / (l::generation_name(legacy) + ".lease"), std::span<const char>{});
			write(directory / (l::generation_name(legacy) + ".image"), flat.bytes()->view());
		}
		if (variant == 11) {
			l::StorageIdentity bad{};
			bad.fill('y');
			const l::detail::SegmentNames broken(bad);
			write(directory / broken.owner_name(),
				  l::encode_segment_ownership(
					  {storage, bad, bytes.size(), l::paged::physical_commitment(bytes)}));
			auto corrupt = bytes;
			corrupt.back() ^= 1;
			write(directory / broken.payload_name(), corrupt);
		}
		if (variant == 13) {
			auto root = image.root();
			++root.cursor.revision;
			const auto encoded = l::paged::encode_root(root);
			l::GenerationId historical{};
			historical.fill('h');
			write(directory / (l::generation_name(historical) + ".image"), encoded);
			write(directory / (l::generation_name(historical) + ".lease"),
				  l::encode_generation_ownership(
					  {storage, historical, encoded.size(), l::paged::physical_commitment(encoded), 2}));
		}
		if (variant == 8) {
			l::GenerationId abandoned{};
			abandoned.fill('u');
			write(directory / (l::generation_name(abandoned) + ".image"), image.bytes()->view());
		}
		auto core = std::make_shared<l::detail::StoreCore>(account, l::DiskStoreOptions{}, domain.factory());
		core->session = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
		core->directory_identity = core->session->directory_identity();
		core->identity = storage;
		core->owner = ::getuid();
		core->selection = {l::SelectionKnowledge::Known, image.descriptor()};
		core->status = l::StoreStatus::Ready;
		core->controls = l::make_collector_controls(core->session);
		l::DiskUsage usage{};
		for (const auto &entry : std::filesystem::directory_iterator(directory)) {
			++usage.entries;
			if (entry.is_regular_file())
				usage.bytes += entry.file_size();
			if (entry.path().extension() == ".lease")
				++usage.generations;
		}

		core->ledger.initialize(usage);
		if (variant == 1 || variant == 12 || variant == 13) {
			l::ViewLimits limits;
			limits.retirement_domain = &domain;
			auto actual = l::recover_generation(directory, limits);
			if (variant == 13) {
				for (auto &entry : core->certificates) {
					entry = domain.make<l::detail::PagedDependencyCertificate>(account, domain.factory());
					entry->capture(actual);
				}
			} else {
				core->retain_recovered(actual);
				if (variant == 12)
					core->drop_certificates();
			}
		}
		if (variant == 2) {
			auto corrupt = std::string(image.bytes()->view());
			corrupt[56] ^= 1;
			write(directory / (l::generation_name(generation) + ".image"), corrupt);
		}
		l::detail::PagedSegmentCollection collector(domain.factory(), core);
		std::optional<d::StorageLedger::Ticket> pressure;
		bool observed_pack = false, pressured = false;
		std::size_t selected_pack_reads = 0;
		for (unsigned attempt = 0; attempt < (variant == 13 ? 2u : 1u); ++attempt) {
			collector.start();
			bool cancelled = false;
			bool injected = false;
			const auto failure = std::make_exception_ptr(std::runtime_error("collector original failure"));
			for (unsigned turns = 0; !collector.done(); ++turns) {
				require(turns < 1000000);
				if (collector.paused()) {
					collector.resume();
					continue;
				}
				if (collector.needs_work()) {
					if (variant == 13 && attempt == 0 && observed_pack && !pressured) {
						pressure.emplace(account->admit(account->limit() - account->used()));
						pressured = true;
					}
					l::ValidationBudget budget{97, 1};
					collector.step(budget);
					continue;
				}
				collector.advance();
				if (collector.done() || collector.needs_work())
					continue;
				auto request = collector.request();
				if (request.kind == c::Kind::OpenInto && request.source.starts_with("segment-"))
					observed_pack = true;
				if (request.kind == c::Kind::Read && request.source.ends_with(".pack") &&
					request.source != names.payload_name())
					++selected_pack_reads;
				collector.submitted();
				c::Result result;
				const bool fail_original =
					!injected && ((variant == 4 && request.kind == c::Kind::Remove) ||
								  (variant == 5 && request.kind == c::Kind::DirectorySync) ||
								  (variant == 6 && request.kind == c::Kind::CloseFile && cancelled) ||
								  (variant == 7 && request.kind == c::Kind::Read));
				if (fail_original && request.kind != c::Kind::CloseFile) {
					result.token = request.token;
					result.error = failure;
					injected = true;
				} else {
					result = kronuz::journal::detail::execute_primitive(collector.io(), request);
					if (fail_original) {
						result.error = failure;
						injected = true;
					}
				}
				if (variant == 6 && request.kind == c::Kind::Remove)
					cancelled = true;

				if (variant == 3 && request.kind == c::Kind::Remove && !cancelled) {
					collector.close();
					cancelled = true;
				}
				c::Result stale;
				stale.token = result.token;
				++stale.token.step;
				require(!collector.complete(std::move(stale)) && collector.in_flight());
				require(collector.complete(std::move(result)));
			}
			if (variant == 13 && attempt == 0) {
				require(pressured && collector.error() && !core->ledger.stats().fenced &&
						core->ledger.stats().used == usage);
				require(std::all_of(core->certificates.begin(), core->certificates.end(),
									[](const auto &entry) { return !entry; }));
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (domain.residents() > 2) {
					require(std::chrono::steady_clock::now() < deadline);
					std::this_thread::yield();
				}
				continue;
			}
			if (variant == 1 || variant == 12)
				require(selected_pack_reads == 0);
			if (variant == 8) {
				require(collector.error() && !core->ledger.stats().fenced &&
						std::filesystem::exists(directory / names.payload_name()) &&
						core->ledger.stats().used == usage);
			} else if (variant >= 4 && variant < 8) {
				require(injected && collector.error() == failure && core->ledger.stats().used == usage);
				require(core->ledger.stats().fenced == (variant != 7));
			} else if (variant == 2) {
				require(bool(collector.error()) &&
						std::filesystem::exists(directory / names.payload_name()) &&
						core->ledger.stats().used == usage);
			} else {
				require(!collector.error() && !std::filesystem::exists(directory / names.payload_name()) &&
						!std::filesystem::exists(directory / names.owner_name()));
				require(core->ledger.stats().used.bytes ==
							usage.bytes - (variant == 9 ? 0 : bytes.size()) - l::segment_ownership_bytes &&
						core->ledger.stats().used.entries == usage.entries - (variant == 9 ? 1 : 2));
			}
			for (const auto &segment : image.segments()) {
				const l::detail::SegmentNames protected_names(segment.ownership.segment);
				require(std::filesystem::exists(directory / protected_names.payload_name()));
			}
			require(!core->controls->active());
		}
	}
	domain.close().wait();
}
