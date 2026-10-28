#include "local_corpus.hh"
#include "mapped_recovery_admission.hh"
#include "paged_delta_plan.hh"
#include "recovery_context.hh"
#include "segment_names.hh"
#include "store_core.hh"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace d = detent::distribution;
namespace l = detent::local;
namespace m = msgpack::bounded;
using Clock = std::chrono::steady_clock;
void check(bool value) {
	if (!value)
		throw std::runtime_error("recovery benchmark invariant failed");
}
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(bytes.data(), bytes.size());
	file.close();
	check(bool(file) && ::chmod(path.c_str(), 0600) == 0);
}
std::string key(unsigned i) {
	char digits[7];
	const auto end = std::to_chars(digits, digits + sizeof digits, i).ptr;
	std::string result = "key";
	result.append(6 - (end - digits), '0');
	result.append(digits, end);
	return result;
}
d::ValidatedDelta delta(d::Cursor base, unsigned changes, std::shared_ptr<d::StorageLedger> heap) {
	std::string wire;
	auto array = [&](unsigned n) { m::AppendPacker(wire).pack_array(n); };
	auto number = [&](std::uint64_t n) { m::AppendPacker(wire).pack_uint64(n); };
	auto cursor = [&](const d::Cursor &value) {
		array(5);
		for (const auto *id : {&value.cluster, &value.group, &value.restore_epoch})
			m::AppendPacker(wire).pack_bin({id->data(), id->size()});
		m::AppendPacker(wire).pack_bin({value.scope.data(), value.scope.size()});
		number(value.revision);
	};
	array(changes ? 7 : 5);
	number(1);
	number(changes ? 2 : 3);
	array(2);
	number(1);
	number(1);
	cursor(base);
	auto target = base;
	target.revision += 2;
	cursor(target);
	if (changes) {
		array(4);
		number(1);
		number(changes);
		number(0);
		number(3 + changes * 13);
		array(1);
		array(3);
		number(base.revision + 1);
		array(1);
		array(2);
		m::AppendPacker(wire).pack_str("app");
		number(base.revision + 1);
		array(changes);
		for (unsigned i = 0; i < changes; ++i) {
			array(4);
			number(0);
			m::AppendPacker(wire).pack_str("app:" + key(i * 4));
			number(0);
			array(2);
			number(1);
			m::AppendPacker(wire).pack_int64(1000000 + i);
		}
	}
	d::Limits limits;
	limits.storage_account = std::move(heap);
	d::DeltaDecoder decoder(base, {"app"}, limits, changes ? d::UpdateKind::Delta : d::UpdateKind::Progress);
	auto input = std::span<const char>(wire);
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
int main(int argc, char **argv) {
	try {
		const auto count = argc > 1 ? std::stoul(argv[1]) : 100000;
		if (!count || count > 500000 || argc > 2)
			throw std::invalid_argument("expected count 1..500000");
		const auto directory = std::filesystem::current_path() / ".scratch" /
							   ("paged-recovery-bench-" + std::to_string(::getpid()));
		struct Cleanup {
			std::filesystem::path path;
			~Cleanup() {
				std::error_code error;
				std::filesystem::remove_all(path, error);
			}
		} cleanup{directory};
		std::filesystem::create_directories(directory);
		check(::chmod(directory.c_str(), 0700) == 0);
		l::StorageIdentity storage{}, generation{};
		storage.fill('s');
		generation.fill('g');
		l::Descriptor original;
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
			original = image.descriptor();
			write(directory / "storage.meta", l::encode_storage_identity(storage));
			write(directory / "selected", l::encode_descriptor(original));
			const auto name = l::generation_name(generation);
			write(directory / (name + ".image"), {image.bytes()->data(), image.bytes()->size()});
			write(
				directory / (name + ".lease"),
				l::encode_generation_ownership({storage, generation, original.length, original.checksum, 2}));
			for (const auto &segment : image.segments()) {
				const l::detail::SegmentNames names(segment.ownership.segment);
				write(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
				write(directory / names.payload_name(), {segment.bytes->data(), segment.bytes->size()});
			}
		}
		auto controls = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		auto heap = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
		auto mappings = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
		d::RetirementRuntime runtime(controls, 1);
		auto domain = runtime.domain(heap, 16384);
		{
			l::ViewLimits limits;
			limits.retirement_domain = &domain;
			limits.mapping_account = mappings;
			auto base = l::recover_generation(directory, limits);
			l::detail::StoreCore core(heap, {}, domain.factory());
			core.retain_recovered(base);
			const auto mapping_baseline = mappings->used();
			for (const unsigned changes : {0, 1, 10, 1000}) {
				if (changes && std::uint64_t(changes - 1) * 4 >= count)
					continue;
				l::GenerationId next;
				next.fill('a');
				next[0] = changes ? (changes == 1 ? 'b' : changes == 10 ? 'c' : 'd') : 'a';
				auto update = delta(original.cursor, changes, heap);
				std::optional<l::detail::PagedPlan> plan;
				if (changes) {
					l::detail::PagedDeltaPlanBuilder builder(base, std::move(update), next, domain);
					while (!builder.done()) {
						l::ValidationBudget budget;
						builder.step(budget);
					}
					plan.emplace(builder.take());
				} else {
					l::detail::PagedPlanBuilder builder(
						base, std::make_shared<const d::ValidatedDelta>(std::move(update)), next, domain);
					while (!builder.done()) {
						l::ValidationBudget budget;
						builder.step(budget);
					}
					plan.emplace(builder.take());
				}
				auto admission =
					domain.make<l::detail::MappedRecoveryAdmission>(heap, mappings, domain.factory());
				admission->populate(*plan, core.verified_paged);
				const auto reserved = admission->reserved_bytes();
				std::uint64_t fresh_bytes = 0;
				const auto name = l::generation_name(next);
				write(directory / (name + ".image"), {plan->bytes()->data(), plan->bytes()->size()});
				write(directory / (name + ".lease"),
					  l::encode_generation_ownership(
						  {storage, next, plan->descriptor().length, plan->descriptor().checksum, 2}));
				for (const auto &segment : plan->segments()) {
					const l::detail::SegmentNames names(segment.ownership.segment);
					write(directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
					write(directory / names.payload_name(), {segment.bytes->data(), segment.bytes->size()});
					fresh_bytes += segment.bytes->size();
				}
				write(directory / "selected", l::encode_descriptor(plan->descriptor()));
				std::optional<l::RecoveredGeneration> warm, cold;
				for (bool reuse : {true, false}) {
					auto metrics = std::make_shared<l::detail::RecoveryMetrics>();
					l::detail::RecoveryContext context{
						domain.factory(),
						heap,
						mappings,
						limits,
						reuse ? core.verified_paged : d::RetirementHandle<l::detail::LeasedPagedRoot>{},
						reuse ? admission : d::RetirementHandle<l::detail::MappedRecoveryAdmission>{}};
					context.metrics = metrics;
					const auto heap_before = heap->used(), mapped_before = mappings->used();
					metrics->sampled_heap_peak = heap_before;
					metrics->sampled_mapping_peak = mapped_before;
					const auto cpu_start = std::clock();
					const auto start = Clock::now();
					auto result = l::detail::RecoveryEngine::recover(directory, std::move(context));
					const auto elapsed =
						std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
					const auto cpu_us = (std::clock() - cpu_start) * 1000000.0 / CLOCKS_PER_SEC;
					check(result.view().size() == count);
					if (reuse) {
						check(metrics->warm && metrics->progress == !changes);
						check(metrics->segment_hash_bytes == 2 * fresh_bytes);
						check(metrics->segment_mapping_installs == plan->segments().size());
						check(metrics->definitions_validated == changes &&
							  metrics->old_definition_headers == changes);
						warm.emplace(std::move(result));
					} else
						cold.emplace(std::move(result));
					std::cout << "{\"records\":" << count << ",\"changes\":" << changes << ",\"mode\":\""
							  << (reuse ? "warm" : "cold") << "\",\"wall_us\":" << elapsed
							  << ",\"process_cpu_us\":" << cpu_us
							  << ",\"root_hash_bytes\":" << metrics->root_hash_bytes
							  << ",\"segment_hash_bytes\":" << metrics->segment_hash_bytes
							  << ",\"segment_mapping_installs\":" << metrics->segment_mapping_installs
							  << ",\"definitions_validated\":" << metrics->definitions_validated
							  << ",\"old_definition_headers\":" << metrics->old_definition_headers
							  << ",\"mapping_reservation_bytes\":" << reserved
							  << ",\"heap_before_bytes\":" << heap_before
							  << ",\"sampled_heap_peak_bytes\":" << metrics->sampled_heap_peak
							  << ",\"mapping_before_bytes\":" << mapped_before
							  << ",\"sampled_mapping_peak_bytes\":" << metrics->sampled_mapping_peak << "}\n";
				}
				for (unsigned i = 0; i < count; ++i) {
					const auto knob = key(i);
					if (i % 4 == 0) {
						const auto expected =
							i / 4 < changes ? std::int64_t(1000000 + i / 4) : -std::int64_t(i);
						check(warm->view().get("app", knob).value() == expected &&
							  cold->view().get("app", knob).value() == expected);
					} else {
						for (const auto scope : {"beta", "production"}) {
							const auto expected = base.view().eval("app", knob, "host-a", scope).value();
							check(warm->view().eval("app", knob, "host-a", scope).value() == expected &&
								  cold->view().eval("app", knob, "host-a", scope).value() == expected);
						}
					}
				}
				write(directory / "selected", l::encode_descriptor(original));
				warm.reset();
				cold.reset();
				admission.reset();
				const auto deadline = Clock::now() + std::chrono::seconds(10);
				while (mappings->used() != mapping_baseline) {
					check(Clock::now() < deadline);
					std::this_thread::yield();
				}
			}
		}
		domain.close().wait();
		check(mappings->used() == 0);
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
