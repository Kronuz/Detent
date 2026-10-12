#include "owned_segment_inputs.hh"
#include "paged_disk_fixture.hh"
int main() {
	auto input = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(input, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	auto account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 32);
	const auto initial = account->used();
	{
		l::detail::OwnedSegmentInputs inputs(domain.factory(), account, storage, 4, 240);
		for (char label : {'c', 'b', 'a'}) {
			l::StorageIdentity id;
			id.fill(label);
			const std::string value(80, label);
			auto bytes = d::make_owned_buffer(account, value.size());
			bytes->append(value);
			bytes->seal();
			inputs.begin({storage, id, value.size(), l::paged::physical_commitment(value)}, bytes);
			while (inputs.busy()) {
				l::ValidationBudget budget{1, 1};
				inputs.step(budget);
			}
		}
		const auto rows = inputs.inputs();
		require(rows.size() == 3 && rows[0].ownership.segment[0] == 'a' &&
				rows[1].ownership.segment[0] == 'b' && rows[2].ownership.segment[0] == 'c');
		for (const auto &row : rows)
			require(row.bytes.front() == row.ownership.segment.front());
		const std::string value(80, 'd');
		auto bytes = d::make_owned_buffer(account, value.size());
		bytes->append(value);
		bytes->seal();
		l::StorageIdentity id;
		id.fill('d');
		bool refused = false;
		try {
			inputs.begin({storage, id, value.size(), l::paged::physical_commitment(value)}, bytes);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		require(refused && inputs.inputs().size() == 3 && !inputs.busy());
	}
	{
		l::detail::OwnedSegmentInputs inputs(domain.factory(), account, storage, image.segments().size(),
											 l::paged::segment_bytes);
		const auto owner = inputs.owner();
		l::detail::PagedDependencyWalk walk(account, owner, l::detail::OwnedSegmentInputs::lookup,
											image.root(), storage);
		for (unsigned turns = 0; !walk.done(); ++turns) {
			require(turns < 1000000);
			l::ValidationBudget budget{1, 1};
			walk.step(budget);
			if (const auto id = walk.missing()) {
				const auto found =
					std::find_if(image.segments().begin(), image.segments().end(),
								 [&](const auto &segment) { return segment.ownership.segment == *id; });
				require(found != image.segments().end());
				auto bytes = d::make_owned_buffer(account, found->bytes->size());
				bytes->append(found->bytes->view());
				bytes->seal();
				inputs.begin(found->ownership, bytes);
				l::ValidationBudget zero{0, 0};
				inputs.step(zero);
				require(inputs.busy());
				while (inputs.busy()) {
					l::ValidationBudget work{1, 1};
					inputs.step(work);
				}
			}
		}
		require(inputs.inputs().size() == image.segments().size());
		inputs.freeze();
		l::detail::GraphValidator graph(account, owner, inputs.inputs(), image.root(), storage);
		while (!graph.done()) {
			l::ValidationBudget budget{1, 1};
			graph.step(budget);
		}
		require(graph.root().logical == image.root().logical && graph.counts().bools == 1 &&
				graph.counts().ints == 1);
		for (;;) {
			l::ValidationBudget budget{1, 1};
			if (graph.discard(budget))
				break;
		}
		for (;;) {
			l::ValidationBudget budget{1, 1};
			if (walk.discard(budget))
				break;
		}
		bool rejected = false;
		try {
			inputs.begin(inputs.inputs()[0].ownership, image.segments()[0].bytes);
		} catch (const std::logic_error &) {
			rejected = true;
		}
		require(rejected);
	}
	domain.close().wait();
	require(account->used() == initial);
}
