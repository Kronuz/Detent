#include "local_corpus.hh"
#include "paged_segment_collection.hh"
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "store_core.hh"
#include <chrono>
#include <completion/native_completion.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace d = detent::distribution;
namespace l = detent::local;
namespace c = kronuz::io::completion;
using Clock = std::chrono::steady_clock;
void check_at(bool value, unsigned line) {
	if (!value)
		throw std::runtime_error("collection benchmark invariant failed at line " + std::to_string(line));
}
#define check(value) check_at((value), __LINE__)
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(bytes.data(), bytes.size());
	file.close();
	check(bool(file) && ::chmod(path.c_str(), 0600) == 0);
}
int main(int argc, char **argv) {
 const char *stage="setup";
	try {
		const auto count = argc > 1 ? std::stoul(argv[1]) : 100000;
		if (argc > 2 || count == 0 || count > 500000)
			throw std::invalid_argument("expected count 1..500000");
		const auto directory = std::filesystem::current_path() / ".scratch" /
							   ("paged-collection-bench-" + std::to_string(::getpid()));
		struct Cleanup {
			std::filesystem::path path;
			~Cleanup() {
				std::error_code error;
				std::filesystem::remove_all(path, error);
			}
		} cleanup{directory};
		std::filesystem::create_directories(directory);
		check(::chmod(directory.c_str(), 0700) == 0);
		l::StorageIdentity storage{};
		storage.fill('s');
		l::GenerationId generation{};
		generation.fill('g');
		l::Descriptor selected;
		std::uint64_t physical = 0, segments = 0;
		{
			auto corpus = detent::bench::local_corpus(count);
			d::SnapshotDecoder decoder(corpus.cursor, {"app"});
			auto input = std::span<const char>(corpus.wire);
			while (!input.empty()) {
				d::WorkBudget budget;
				decoder.feed(input, budget);
			}
			auto source = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
			l::detail::PagedSnapshotBuilder builder(source, storage, generation);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			auto image = builder.take();
			selected = image.descriptor();
			segments = image.segments().size();
			write(directory / "storage.meta", l::encode_storage_identity(storage));
			write(directory / (l::generation_name(generation) + ".image"), image.bytes()->view());
			write(
				directory / (l::generation_name(generation) + ".lease"),
				l::encode_generation_ownership({storage, generation, selected.length, selected.checksum, 2}));
			for (const auto &segment : image.segments()) {
				const l::detail::SegmentNames names(segment.ownership.segment);
				write(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
				write(directory / names.payload_name(), segment.bytes->view());
				physical += segment.ownership.length;
			}
			write(directory / "selected", l::encode_descriptor(selected));
			auto historical = image.root();
			++historical.cursor.revision;
			l::GenerationId id{};
			id.fill('h');
			const auto root = l::paged::encode_root(historical);
			write(directory / (l::generation_name(id) + ".image"), root);
			write(directory / (l::generation_name(id) + ".lease"),
				  l::encode_generation_ownership(
					  {storage, id, root.size(), l::paged::physical_commitment(root), 2}));
		}
		auto account = std::make_shared<d::StorageLedger>(512ull * l::paged::segment_bytes);
		auto maps = std::make_shared<d::StorageLedger>(256ull * l::paged::segment_bytes);
		auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 4096);
		{
			auto core =
				std::make_shared<l::detail::StoreCore>(account, l::DiskStoreOptions{}, domain.factory());
			core->session = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
			core->controls = l::make_collector_controls(core->session);
			core->directory_identity = core->session->directory_identity();
			core->identity = storage;
			core->owner = ::getuid();
			core->selection = {l::SelectionKnowledge::Known, selected};
			core->status = l::StoreStatus::Ready;
			l::ViewLimits limits;
			limits.retirement_domain = &domain;
			limits.mapping_account = maps;
			{
				stage="selected recovery";
 auto actual = l::recover_generation(directory, limits);
				check(actual.view().size() == count);
				core->retain_recovered(actual);
			}
			l::DiskUsage usage{};
			for (const auto &entry : std::filesystem::directory_iterator(directory)) {
				++usage.entries;
				if (entry.is_regular_file())
					usage.bytes += entry.file_size();
				if (entry.path().extension() == ".lease")
					++usage.generations;
			}
			core->ledger.initialize(usage);
			l::detail::PagedSegmentCollection collector(domain.factory(), core);
			const auto residents = domain.residents();
			for (unsigned run = 0; run < 2; ++run) {
				const auto before = account->used();
				const auto live_mapping_before = maps->used();
				check(live_mapping_before >= physical);
				auto peak = before;
				std::uint64_t originals = 0, payload_reads = 0, cpu_turns = 0, max_turn_us = 0;
				const auto start = Clock::now();
				stage=run?"cached collection":"cold collection";
 collector.start();
				while (!collector.done()) {
					const auto turn = Clock::now();
					if (collector.paused())
						collector.resume();
					else if (collector.needs_work()) {
						l::ValidationBudget budget;
						collector.step(budget);
						++cpu_turns;
					} else {
						collector.advance();
						if (!collector.done() && !collector.needs_work()) {
							const auto request = collector.request();
							collector.submitted();
							check(collector.complete(
								kronuz::journal::detail::execute_primitive(collector.io(), request)));
							++originals;
							if (request.kind == c::Kind::Read && request.source.ends_with(".pack"))
								++payload_reads;
						}
					}
					peak = std::max(peak, account->used());
					max_turn_us = std::max(
						max_turn_us,
						static_cast<std::uint64_t>(
							std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - turn)
								.count()));
				}
				if (collector.error())
					std::rethrow_exception(collector.error());
				check(core->ledger.stats().used == usage && maps->used() == live_mapping_before);
				if (run)
					check(payload_reads == 0);
				const auto elapsed =
					std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
				std::cout << "{\"records\":" << count << ",\"cache\":\"" << (run ? "hit" : "miss")
						  << "\",\"segments\":" << segments << ",\"pack_bytes\":" << physical
						  << ",\"live_mapping_bytes\":" << maps->used() << ",\"heap_before\":" << before
						  << ",\"heap_sampled_peak\":" << peak << ",\"heap_global_peak\":" << account->peak()
						  << ",\"originals\":" << originals << ",\"payload_reads\":" << payload_reads
						  << ",\"cpu_turns\":" << cpu_turns << ",\"max_turn_us\":" << max_turn_us
						  << ",\"wall_us\":" << elapsed << "}\n";
				const auto deadline = Clock::now() + std::chrono::seconds(10);
				while (domain.residents() > residents + 2) {
					check(Clock::now() < deadline);
					std::this_thread::yield();
				}
			}
		}
		domain.close().wait();
		check(maps->used() == 0);
	} catch (const std::exception &error) {
		std::cerr << stage << ": " << error.what() << '\n';
		return 1;
	}
}
