#include "detent_distribution/framing.hh"
#include "detent_relay/bundle.hh"
#include "paged_disk_fixture.hh"
#include "state_equivalence.hh"
#include <source_location>
namespace r = detent::relay;
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("bundle maintenance invariant at " + std::to_string(location.line()));
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("bundle-maintenance-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	auto heap = std::make_shared<d::StorageLedger>(32 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
	d::RetirementRuntime runtime(controls, 1);
	auto domain = runtime.domain(heap, 256);
	{
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId flat_id{}, paged_id{};
		flat_id.fill('f');
		paged_id.fill('p');
		l::StorageIdentity storage{};
		storage.fill('s');
		l::ImageBuilder builder(source, flat_id);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		std::filesystem::create_directories(directory);
		check(::chmod(directory.c_str(), 0700) == 0);
		const auto name = l::generation_name(flat_id);
		write(directory / (name + ".image"), *builder.bytes());
		write(directory / (name + ".lease"), {});
		write(directory / "selected", l::encode_descriptor(builder.descriptor()));
		auto flat = l::recover_generation(directory, limits);
		r::BundleOptions options;
		options.configured = cursor();
		options.incarnation.fill('i');
		options.namespaces = 1;
		r::Bundle bundle(options, domain);
		bundle.authenticated();
		bundle.selected(flat);
		const auto before = bundle.offer(cursor());
		l::detail::PagedSnapshotBuilder replacement(flat, heap, storage, paged_id);
		while (!replacement.done()) {
			l::ValidationBudget budget;
			replacement.step(budget);
		}
		install(directory, replacement.take(), storage);
		auto paged = l::recover_generation(directory, limits);
		{
			d::WorkBudget budget{97, 1};
			check(!bundle.snapshot_step(budget));
		}
		bool refused = false;
		try {
			bundle.selected(paged);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused && bundle.status().gaps == 1);
		l::detail::StateEquivalence wrong(paged, paged, heap);
		auto stale = wrong.take_proof();
		refused = false;
		try {
			bundle.selected_equivalent(paged, stale);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused);
		l::detail::StateEquivalence equality(flat, paged, heap);
		while (!equality.done()) {
			l::ValidationBudget budget{1, 1};
			equality.step(budget);
		}
		auto proof = equality.take_proof();
		bundle.selected_equivalent(paged, proof);
		const auto after = bundle.offer(cursor());
		check(after.decision == before.decision && after.marker == before.marker &&
			  bundle.status().gaps == 1);
		unsigned turns = 0;
		for (;;) {
			d::WorkBudget budget{97, 1};
			if (bundle.snapshot_step(budget))
				break;
			check(++turns < 1000000);
		}
		auto offer = bundle.offer({}, true);
		check(offer.decision == d::CacheDecision::Update && offer.update->target() == cursor());
		d::FragmentDecoder decoder(offer.update->kind(), offer.update->logical_bytes(), false, heap);
		std::string wire;
		for (std::size_t i = 0; i < offer.update->fragments(); ++i) {
			auto frame = offer.update->frame(i, false);
			auto bytes = std::span<const char>(frame->view());
			while (!bytes.empty()) {
				d::WorkBudget budget{97, 1};
				decoder.feed(bytes, budget);
				if (auto fragment = decoder.take())
					wire.append(fragment->bytes());
			}
		}
		decoder.finish();
		check(wire == fixture() && bundle.status().snapshots == 1);
		refused = false;
		try {
			bundle.selected_equivalent(paged, proof);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		check(refused);
		bundle.fence();
	}
	domain.close().wait();
	check(mappings->used() == 0);
}
