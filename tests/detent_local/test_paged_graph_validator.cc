#include "dense_graph_fixture.hh"
#include <iostream>
using namespace dense_graph_test;
namespace {
void run(l::detail::GraphValidator &validator, std::size_t bytes = 1) {
	for (std::size_t steps = 0; !validator.done(); ++steps) {
		check(steps < 2000000);
		l::ValidationBudget budget{bytes, 1};
		validator.step(budget);
		check(budget.bytes <= bytes && budget.rows <= 1);
	}
}
void reject(const std::shared_ptr<Fixture> &source) {
	rejects<std::invalid_argument>([&] {
		l::detail::GraphValidator validator(source->account, source, source->inputs, source->root,
		                                    source->storage);
		run(validator);
	});
}
} // namespace
int main() {
	auto account = std::make_shared<d::StorageLedger>(32 * p::segment_bytes);
	for (auto count : {0u, 1u, 32u, 256u, 1000u}) {
		auto source = fixture(count);
		const auto baseline = account->used();
		{
			l::detail::GraphValidator validator(account, source, source->inputs, source->root,
			                                    source->storage);
			l::ValidationBudget empty{0, 0};
			validator.step(empty);
			check(!validator.done());
			rejects<std::logic_error>([&] { validator.root(); });
			const auto before = p::hash_counts();
			run(validator);
			const auto after = p::hash_counts();
			check(after.page_calls == before.page_calls + source->pages.size());
			check(after.pack_calls == before.pack_calls + 1);
			check(validator.counts().ints == count && !validator.counts().bools);
			check(validator.segment_reachability(0).pages == source->pages.size());
			check(validator.segment_reachability(0).bytes == source->root.bytes);
			check(p::state_commitment(validator.root()) == p::state_commitment(source->root));
			check(!validator.discard(empty));
			l::ValidationBudget budget;
			check(validator.discard(budget));
		}
		check(account->used() == baseline);
	}
	{
		auto source = fixture();
		l::detail::GraphValidator validator(
		    account, source, {},
		    [&] {
			    auto empty = source->root;
			    empty.tree = {};
			    empty.pages = empty.bytes = empty.segments = empty.records = empty.namespaces = 0;
			    return empty;
		    }(),
		    source->storage);
		run(validator);
		check(p::state_commitment(validator.root()) == p::empty_commitment());
	}
	for (auto field : {16u, 24u}) {
		auto source = fixture(1000);
		const auto root_offset = source->root.tree.offset;
		const auto header = p::decode_page_header(
		    std::span<const char>(source->bytes).subspan(root_offset, source->root.tree.length));
		check(header.externals > 0);
		// Change a child's summary and rehash its parent, preserving the file
		// checksum.
		const auto content =
		    root_offset + source->root.tree.length - p::child_bytes * header.externals;
		if (field == 16)
			++source->bytes[content + 39];
		else
			++source->bytes[content + 44];
		auto root_page =
		    std::span<char>(source->bytes).subspan(root_offset, source->root.tree.length);
		source->root.tree.digest = p::page_commitment(root_page);
		std::copy(source->root.tree.digest.begin(), source->root.tree.digest.end(),
		          root_page.begin() + 32);
		source->refresh();
		reject(source);
	}
	{
		auto source = fixture(1000);
		++source->inputs[0].ownership.checksum[0];
		reject(source);
	}
	{
		auto source = fixture();
		l::detail::GraphValidator validator(account, source, source->inputs, source->root,
		                                    source->storage);
		run(validator);
		d::RetirementRuntime runtime{account, 1};
		auto domain = runtime.domain(account, 16);
		auto view = validator.take(domain);
		check(view.size() == 32 && view.contains("ns", "k100000"));
		check(view.get("ns", "k100000").value() == 0);
		check(view.get("ns", "k100031").value() == 31);
		view = {};
		domain.close().wait();
	}
	check(!account->used());
	std::cout << "dense graph semantics, summary binding, counts and cleanup passed\n";
}
