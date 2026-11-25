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
				a(4);
				m::signed_integer(wire, INT64_MIN);
				wire.push_back('\xc0');
				wire.push_back('\xc0');
				a(0);
				a(4);
				n(i);
				text(scope[i] + ":flag");
				n(change == 2 ? 40 : 41);
				a(4);
				n(1);
				if (change == 3)
					wire.push_back('\xc0');
				else
					n(percentage);
				n(1);
				a(1);
				a(3);
				text(group);
				n(change == 4 ? 0 : 1);
				n(change == 4 ? 1 : 17);
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
		auto build = [&](std::shared_ptr<const d::ValidatedSnapshot> input, char id) {
			l::GenerationId generation{};
			generation.fill(id);
			l::detail::PagedSnapshotBuilder builder(std::move(input), generation);
			while (!builder.done()) {
				l::ValidationBudget budget{1, 1};
				builder.step(budget);
			}
			return builder.take();
		};
		auto paged_image = build(source, 'p');
		install(directory / "paged", paged_image);
		auto paged = l::recover_generation(directory / "paged", limits);
		compare(paged, paged, heap, true);
		{
			auto pressure = heap->admit(heap->limit() - heap->used());
			compare(paged, paged, heap, true);
		}
		auto again_image = build(source, 'q');
		install(directory / "again", again_image);
		auto again = l::recover_generation(directory / "again", limits);
		compare(paged, again, heap, true);
		compare(again, paged, heap, true);
		const auto mapping_baseline = mappings->used();
		if (namespace_count) {
			for (unsigned change = 1; change <= 4; ++change) {
				const auto output = directory / ("change-" + std::to_string(change));
				install(output, build(std::make_shared<const d::ValidatedSnapshot>(input(25, change)), 'c'));
				auto altered = l::recover_generation(output, limits);
				l::detail::StateEquivalence equality(paged, altered, heap);
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
		{
			l::GenerationId replacement{};
			replacement.fill('b');
			l::detail::PagedSnapshotBuilder borrowed(paged, heap, replacement);
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
			state_check(l::paged::state_commitment(image.root()) == l::paged::state_commitment(paged_image.root()));
			const auto output = directory / "replacement";
			l::detail::CandidatePagedValidator validator(std::move(image), domain.factory(), heap, limits);
			while (!validator.done()) {
				l::ValidationBudget budget{1, 1};
				validator.step(budget);
			}
			state_check(validator.counts().records == 2 * namespace_count);
			auto owner = validator.image();
			l::detail::StateEquivalence equality(l::detail::StateCursor(paged, heap),
												 l::detail::StateCursor(validator.take(), heap));
			state_check(equality.equal());
			auto evidence = equality.take_proof();
			install(output, owner->image());
			auto recovered = l::recover_generation(output, limits);
			state_check(evidence.binds(paged, recovered));
			compare(paged, recovered, heap, true);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (mappings->used() != mapping_baseline) {
			state_check(std::chrono::steady_clock::now() < deadline);
			std::this_thread::yield();
		}
		for (unsigned corruption = 0; corruption < (namespace_count ? 2u : 1u); ++corruption) {
			l::GenerationId bad_id{};
			bad_id.fill('z');
			l::detail::PagedSnapshotBuilder builder(paged, heap, bad_id);
			while (!builder.done()) {
				l::ValidationBudget budget{1, 1};
				builder.step(budget);
			}
			auto image = builder.take();
			// A wrong root checksum, then a broken page: the first page of a pack starts at its offset 0, and its
			// magic is decoded on every walk (an own build's digests are trusted, so a digest flip is the scrub's).
			if (!corruption)
				const_cast<l::Descriptor &>(image.descriptor()).checksum[0] ^= 1;
			else
				const_cast<char *>(image.segments()[0].bytes->data())[0] ^= 1;
			const auto before = mappings->used();
			bool refused = false;
			try {
				l::detail::CandidatePagedValidator validator(std::move(image), domain.factory(), heap, limits);
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
			state_check(l::read_generation_selection(directory / "paged").descriptor == paged.descriptor());
		}

		auto changed_image = build(std::make_shared<const d::ValidatedSnapshot>(input(26)), 'x');
		install(directory / "changed", changed_image);
		auto changed = l::recover_generation(directory / "changed", limits);
		compare(paged, changed, heap, namespace_count == 0);
		const auto reader_directory = directory / "reader";
		install(reader_directory, paged_image);
		l::GenerationReader reader(reader_directory, limits);
		state_check(reader.refresh());
		auto pinned = reader.read_view();
		install(reader_directory, again_image);
		state_check(reader.refresh() && reader.descriptor() == again.descriptor());
		install(reader_directory, changed_image);
		bool rejected = false;
		try {
			reader.refresh();
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		state_check(rejected == (namespace_count != 0));
		state_check(reader.descriptor() == (namespace_count ? again.descriptor() : changed.descriptor()));
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
