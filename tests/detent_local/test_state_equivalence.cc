#include "detent_local/reader.hh"
#include "paged_disk_fixture.hh"
#include "state_equivalence.hh"
#include <chrono>
void compare(const l::RecoveredGeneration &left, const l::RecoveredGeneration &right,
			 const std::shared_ptr<d::StorageLedger> &heap, bool expected) {
	l::detail::StateEquivalence proof(left, right, heap);
	unsigned turns = 0;
	while (!proof.done()) {
		require(++turns < 100000);
		l::ValidationBudget budget{1, 1};
		proof.step(budget);
	}
	require(proof.equal() == expected);
}
void run(bool nul_key) {
	const auto directory =
		std::filesystem::current_path() / ".scratch" /
		("state-equivalence-" + std::to_string(::getpid()) + "-" + std::to_string(nul_key));
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
		auto input = [&](unsigned percentage) {
			auto wire = fixture(1, nul_key ? 25 : 24, percentage);
			if (nul_key) {
				const auto pos = wire.find(std::string("\xa4"
													   "beta",
													   5));
				require(pos != std::string::npos);
				wire.replace(pos, 5,
							 std::string("\xa5"
										 "b\0eta",
										 6));
			}
			return decode(wire);
		};
		auto source = std::make_shared<const d::ValidatedSnapshot>(input(25));
		l::GenerationId flat_id{}, paged_id{}, changed_id{};
		flat_id.fill('f');
		paged_id.fill('p');
		changed_id.fill('x');
		l::StorageIdentity storage{};
		storage.fill('s');
		l::ImageBuilder flat_builder(source, flat_id);
		while (!flat_builder.done()) {
			l::ValidationBudget budget{1, 1};
			flat_builder.step(budget);
		}
		auto flat_directory = directory / "flat";
		std::filesystem::create_directories(flat_directory);
		require(::chmod(flat_directory.c_str(), 0700) == 0);
		const auto name = l::generation_name(flat_id);
		write(flat_directory / (name + ".image"), *flat_builder.bytes());
		write(flat_directory / (name + ".lease"), {});
		write(flat_directory / "selected", l::encode_descriptor(flat_builder.descriptor()));
		auto flat = l::recover_generation(flat_directory, limits);
		l::detail::PagedSnapshotBuilder builder(source, storage, paged_id);
		while (!builder.done()) {
			l::ValidationBudget budget{1, 1};
			builder.step(budget);
		}
		auto paged_image = builder.take();
		install(directory / "paged", paged_image, storage);
		auto paged = l::recover_generation(directory / "paged", limits);
		compare(flat, flat, heap, true);
		compare(flat, paged, heap, true);
		compare(paged, flat, heap, true);
		compare(paged, paged, heap, true);
		{
			auto pressure = heap->admit(heap->limit() - heap->used());
			compare(paged, paged, heap, true);
		}
		for (const auto *base : {&flat, &paged}) {
			l::GenerationId replacement{};
			replacement.fill(base == &flat ? 'm' : 'b');
			l::detail::PagedSnapshotBuilder borrowed(*base, heap, storage, replacement);
			l::ValidationBudget zero{0, 0};
			borrowed.step(zero);
			require(!borrowed.done());
			unsigned turns = 0;
			while (!borrowed.done()) {
				require(++turns < 1000000);
				l::ValidationBudget budget{1, 1};
				borrowed.step(budget);
			}
			auto image = borrowed.take();
			require(image.root().logical == paged_image.root().logical);
			const auto output = directory / (base == &flat ? "migration" : "replacement");
			install(output, image, storage);
			auto recovered = l::recover_generation(output, limits);
			compare(*base, recovered, heap, true);
		}
		auto changed_source = std::make_shared<const d::ValidatedSnapshot>(input(26));
		l::detail::PagedSnapshotBuilder changed_builder(changed_source, storage, changed_id);
		while (!changed_builder.done()) {
			l::ValidationBudget budget{1, 1};
			changed_builder.step(budget);
		}
		auto changed_image = changed_builder.take();
		install(directory / "changed", changed_image, storage);
		auto changed = l::recover_generation(directory / "changed", limits);
		compare(flat, changed, heap, false);
		compare(paged, changed, heap, false);
		l::GenerationReader reader(flat_directory, limits);
		require(reader.refresh());
		auto pinned = reader.read_view();
		install(flat_directory, paged_image, storage);
		require(reader.refresh() && reader.descriptor() == paged.descriptor());
		install(flat_directory, changed_image, storage);
		bool rejected = false;
		try {
			reader.refresh();
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected && reader.descriptor() == paged.descriptor());
		require(pinned.get("app", "count").value() == reader.read_view().get("app", "count").value());
	}
	domain.close().wait();
	require(mappings->used() == 0);
}

int main() {
	run(false);
	run(true);
}
