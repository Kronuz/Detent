#include "candidate_flat_validator.hh"
#include "candidate_paged_validator.hh"
#include "detent_local/reader.hh"
#include "paged_disk_fixture.hh"
#include "state_equivalence.hh"
#include <chrono>
#include <source_location>
#include <thread>
void state_check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("state equivalence invariant at " + std::to_string(location.line()));
}
void compare(const l::RecoveredGeneration &left, const l::RecoveredGeneration &right,
			 const std::shared_ptr<d::StorageLedger> &heap, bool expected) {
	l::detail::StateEquivalence proof(left, right, heap);
	unsigned turns = 0;
	while (!proof.done()) {
		state_check(++turns < 100000);
		l::ValidationBudget budget{1, 1};
		proof.step(budget);
	}
	state_check(proof.equal() == expected);
	if (expected) {
		auto evidence = proof.take_proof();
		state_check(evidence.binds(left, right));
		auto moved = std::move(evidence);
		state_check(!evidence.binds(left, right) && moved.binds(left, right));
	}
}
void run(bool nul_key, unsigned namespace_count) {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("state-equivalence-" + std::to_string(::getpid()) + "-" +
							std::to_string(nul_key) + "-" + std::to_string(namespace_count));
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
		std::vector<std::string> scope;
		if (namespace_count)
			scope.push_back("app");
		if (namespace_count > 1)
			scope.push_back("app.sub");
		auto input = [&](unsigned percentage, unsigned change = 0) {
			std::string wire;
			auto a = [&](unsigned count) { m::array(wire, count); };
			auto n = [&](std::uint64_t value) { m::unsigned_integer(wire, value); };
			auto text = [&](std::string_view value) { m::string(wire, value); };
			const auto group = nul_key ? std::string_view("b\0eta", 5) : std::string_view("beta");
			a(7);
			n(1);
			n(1);
			a(2);
			n(1);
			n(1);
			a(5);
			const auto identity = cursor();
			for (const auto *field : {&identity.cluster, &identity.group, &identity.restore_epoch})
				m::string(wire, {field->data(), field->size()}, true);
			m::string(wire, {identity.scope.data(), identity.scope.size()}, true);
			n(identity.revision);
			std::uint64_t strings = 0;
			for (const auto &name : scope)
				strings += 3 * name.size() + 11 + group.size();
			a(4);
			n(scope.size());
			n(2 * scope.size());
			n(scope.size());
			n(strings);
			a(scope.size());
			for (const auto &name : scope) {
				a(2);
				text(name);
				n(change == 1 ? 41 : 42);
			}
			a(2 * scope.size());
			for (unsigned i = 0; i < scope.size(); ++i) {
				a(4);
				n(i);
				text(scope[i] + ":count");
				n(41);
				a(2);
				n(1);
				m::signed_integer(wire, INT64_MIN);
				a(4);
				n(i);
				text(scope[i] + ":flag");
				n(change == 2 ? 40 : 41);
				a(4);
				n(0);
				wire.push_back('\xc3');
				if (change == 3)
					wire.push_back('\xc0');
				else
					n(percentage);
				a(1);
				a(3);
				text(group);
				n(change == 4 ? 0 : 1);
				if (change == 4)
					wire.push_back('\xc3');
				else
					n(17);
			}
			d::Limits decoder_limits;
			decoder_limits.storage_account = heap;
			d::SnapshotDecoder decoder(cursor(), scope, decoder_limits);
			auto bytes = std::span<const char>(wire);
			while (!bytes.empty()) {
				d::WorkBudget budget{1, 1};
				decoder.feed(bytes, budget);
			}
			return decoder.finish();
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
		state_check(::chmod(flat_directory.c_str(), 0700) == 0);
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
		const auto mapping_baseline = mappings->used();
		compare(flat, flat, heap, true);
		compare(flat, paged, heap, true);
		compare(paged, flat, heap, true);
		compare(paged, paged, heap, true);
		{
			const auto before = mappings->used();
			l::detail::CandidateFlatValidator validator(flat_builder, domain.factory(), heap, limits);
			while (!validator.done()) {
				l::ValidationBudget budget{1, 1};
				validator.step(budget);
			}
			auto candidate = validator.take();
			state_check(mappings->used() == before);
			l::detail::StateEquivalence equality(l::detail::StateCursor(paged, heap),
												 l::detail::StateCursor(std::move(candidate), heap));
			while (!equality.done()) {
				l::ValidationBudget budget{1, 1};
				equality.step(budget);
			}
			state_check(equality.equal());
			auto evidence = equality.take_proof();
			state_check(evidence.binds(paged, flat) && !evidence.binds(flat, paged));
		}
		{
			auto pressure = heap->admit(heap->limit() - heap->used());
			compare(paged, paged, heap, true);
		}
		{
			const auto before = mappings->used();
			{
				auto pressure = heap->admit(heap->limit() - heap->used());
				bool refused = false;
				try {
					l::detail::CandidateFlatValidator validator(flat_builder, domain.factory(), heap, limits);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				state_check(refused && mappings->used() == before);
			}
			l::GenerationId candidate_id{};
			candidate_id.fill('c');
			std::optional<l::ImageBuilder> staged(std::in_place, source, candidate_id);
			while (!staged->done()) {
				l::ValidationBudget budget{1, 1};
				staged->step(budget);
			}
			auto captured = l::detail::CandidateFlatInput::capture(*staged);
			for (;;) {
				l::ValidationBudget budget{1, 1};
				if (staged->discard(budget))
					break;
			}
			staged.reset();
			l::detail::CandidateFlatValidator validator(std::move(captured), domain.factory(), heap, limits);
			while (!validator.done()) {
				l::ValidationBudget budget{1, 1};
				validator.step(budget);
			}
			l::detail::StateEquivalence equality(l::detail::StateCursor(flat, heap),
												 l::detail::StateCursor(validator.take(), heap));
			while (!equality.done()) {
				l::ValidationBudget budget{1, 1};
				equality.step(budget);
			}
			state_check(equality.equal() && mappings->used() == before);
			state_check(l::read_generation_selection(flat_directory).descriptor == flat.descriptor());
		}
		if (namespace_count) {
			for (unsigned change = 1; change <= 4; ++change) {
				auto altered = std::make_shared<const d::ValidatedSnapshot>(input(25, change));
				l::GenerationId candidate_id{};
				candidate_id.fill('d' + change);
				l::ImageBuilder output(altered, candidate_id);
				while (!output.done()) {
					l::ValidationBudget budget{1, 1};
					output.step(budget);
				}
				l::detail::CandidateFlatValidator validator(output, domain.factory(), heap, limits);
				while (!validator.done()) {
					l::ValidationBudget budget{1, 1};
					validator.step(budget);
				}
				l::detail::StateEquivalence equality(l::detail::StateCursor(flat, heap),
													 l::detail::StateCursor(validator.take(), heap));
				while (!equality.done()) {
					l::ValidationBudget budget{1, 1};
					equality.step(budget);
				}
				state_check(!equality.equal());
				bool refused = false;
				try {
					equality.take_proof();
				} catch (const std::logic_error &) {
					refused = true;
				}
				state_check(refused);
			}
		}
		for (const auto *base : {&flat, &paged}) {
			l::GenerationId replacement{};
			replacement.fill(base == &flat ? 'm' : 'b');
			l::detail::PagedSnapshotBuilder borrowed(*base, heap, storage, replacement);
			l::ValidationBudget zero{0, 0};
			borrowed.step(zero);
			state_check(!borrowed.done());
			unsigned turns = 0;
			while (!borrowed.done()) {
				state_check(++turns < 1000000);
				l::ValidationBudget budget{1, 1};
				borrowed.step(budget);
			}
			auto image = borrowed.take();
			state_check(image.root().logical == paged_image.root().logical);
			const auto output = directory / (base == &flat ? "migration" : "replacement");
			l::detail::CandidatePagedValidator validator(std::move(image), domain.factory(), heap, storage,
														 limits);
			while (!validator.done()) {
				l::ValidationBudget budget{1, 1};
				validator.step(budget);
			}
			state_check(validator.counts().bools == namespace_count &&
						validator.counts().ints == namespace_count);
			auto owner = validator.image();
			l::detail::StateEquivalence equality(l::detail::StateCursor(*base, heap),
												 l::detail::StateCursor(validator.take(), heap));
			while (!equality.done()) {
				l::ValidationBudget budget{1, 1};
				equality.step(budget);
			}
			state_check(equality.equal());
			auto evidence = equality.take_proof();
			install(output, owner->image(), storage);
			auto recovered = l::recover_generation(output, limits);
			state_check(evidence.binds(*base, recovered));
			compare(*base, recovered, heap, true);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (mappings->used() != mapping_baseline) {
			state_check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		for (unsigned corruption = 0; corruption < (namespace_count ? 2u : 1u); ++corruption) {
			l::GenerationId bad_id{};
			bad_id.fill('z');
			l::detail::PagedSnapshotBuilder builder(flat, heap, storage, bad_id);
			while (!builder.done()) {
				l::ValidationBudget budget{1, 1};
				builder.step(budget);
			}
			auto image = builder.take();
			if (!corruption)
				const_cast<l::Descriptor &>(image.descriptor()).checksum[0] ^= 1;
			else
				const_cast<char *>(
					image.segments()[0].bytes->data())[image.segments()[0].bytes->size() - 1] ^= 1;
			const auto before = mappings->used();
			bool refused = false;
			try {
				l::detail::CandidatePagedValidator validator(std::move(image), domain.factory(), heap,
															 storage, limits);
				while (!validator.done()) {
					l::ValidationBudget budget{1, 1};
					validator.step(budget);
				}
			} catch (const std::runtime_error &) {
				refused = true;
			} catch (const std::invalid_argument &) {
				refused = true;
			}
			state_check(refused && mappings->used() == before);
			state_check(l::read_generation_selection(flat_directory).descriptor == flat.descriptor());
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
		compare(flat, changed, heap, namespace_count == 0);
		compare(paged, changed, heap, namespace_count == 0);
		l::GenerationReader reader(flat_directory, limits);
		state_check(reader.refresh());
		auto pinned = reader.read_view();
		install(flat_directory, paged_image, storage);
		state_check(reader.refresh() && reader.descriptor() == paged.descriptor());
		install(flat_directory, changed_image, storage);
		bool rejected = false;
		try {
			reader.refresh();
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		state_check(rejected == (namespace_count != 0));
		state_check(reader.descriptor() == (namespace_count ? paged.descriptor() : changed.descriptor()));
		if (namespace_count)
			state_check(pinned.get("app", "count").value() == reader.read_view().get("app", "count").value());
		else
			state_check(!pinned.contains("app", "count") && !reader.read_view().contains("app", "count"));
	}
	domain.close().wait();
	state_check(mappings->used() == 0);
}

int main() {
	run(false, 0);
	run(false, 1);
	run(true, 1);
	run(false, 2);
	run(true, 2);
}
