#include "export_capture.hh"
#include <iostream>
namespace a = detent::authority;
namespace d = detent::distribution;
void check(bool value) {
	if (!value)
		throw std::runtime_error("public export capture invariant");
}
int main() {
	try {
		a::Context context;
		context.cluster.fill('c');
		context.group.fill('g');
		context.epoch.fill('e');
		a::State state(context);
		a::Command command;
		command.context = context;
		command.producer = "writer";
		command.kind = a::Kind::Open;
		command.nonce.fill('n');
		check(state.apply(1, command).generation == 1);
		command.kind = a::Kind::Transaction;
		command.nonce = {};
		command.generation = 1;
		std::uint64_t index = 1;
		auto put = [&](std::string ns, std::optional<detent::KnobDef> value) {
			command.name_space = std::move(ns);
			++command.sequence;
			command.mutations = {{command.name_space + ":key", std::move(value), {}, {}}};
			check(state.apply(++index, command).status == a::Status::Applied);
		};
		put("a", detent::IntDef{10});
		put("ab", detent::IntDef{20});
		put("secret", detent::IntDef{30});
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		std::set<std::string> allowed{"a", "ab"};
		std::vector<std::string> requested{"ab", "a"};
		auto scope = a::AuthorizedScope::authorize("reader", 1, requested, allowed, account);
		auto capture = state.capture(scope);
		check(capture->cursor().revision == 3 && capture->cursor().scope == scope->identity());
		check(capture->namespace_revision(0) == 1 && capture->namespace_revision(1) == 2);
		put("a", detent::IntDef{100});
		put("ab", {});
		while (state.reclamation_pending())
			state.collect(128);
		for (std::uint32_t ns = 0; ns < 2; ++ns) {
			auto rows = capture->records(ns);
			auto row = rows.next();
			check(bool(row));
			check(row->namespace_index == ns && row->key == (ns == 0 ? "a:key" : "ab:key"));
			check(row->revision == ns + 1 &&
				  std::get<detent::IntDef>(*row->definition).default_value == (ns == 0 ? 10 : 20));
			check(!rows.next() && !rows.next());
		}
		auto rows = capture->records(0);
		capture.reset();
		scope.reset();
		// The cursor retains both names and root after losing its capture facade.
		auto row = rows.next();
		check(row && row->key == "a:key" && std::get<detent::IntDef>(*row->definition).default_value == 10);
		requested.clear();
		scope = a::AuthorizedScope::authorize("reader", 1, requested, allowed, account);
		capture = state.capture(scope);
		check(capture->scope().size() == 0 && capture->cursor().revision == 5);
		bool rejected = false;
		try {
			capture->records(0);
		} catch (const std::out_of_range &) {
			rejected = true;
		}
		check(rejected);

		{
			a::State old(context);
			a::Command open;
			open.context = context;
			open.producer = "writer";
			open.kind = a::Kind::Open;
			open.nonce.fill('n');
			check(old.apply(1, open).generation == 1);
			detent::BoolDef flag;
			for (unsigned group = 0; group < 256; ++group)
				flag.overrides.emplace("group" + std::to_string(group), true);
			a::Command update;
			update.context = context;
			update.producer = "writer";
			update.generation = 1;
			update.sequence = 1;
			update.name_space = "a";
			update.mutations = {{"a:key", flag, {}, {}}};
			check(old.apply(2, update).status == a::Status::Applied);
			std::vector<std::string> names{"a"};
			auto authorized = a::AuthorizedScope::authorize("reader", 1, names, allowed, account);
			auto pinned = old.capture(authorized);
			std::optional<a::ExportCapture::RecordCursor> held;
			held.emplace(pinned->records(0));
			old = a::State(context); // Replacement snapshot uses independent arenas.
			auto value = held->next();
			check(value && std::get<detent::BoolDef>(*value->definition).overrides.size() == 256);
			held.reset();
			pinned->retire();
			check(pinned->reclamation_pending());
			std::size_t turns = 0;
			while (pinned->reclamation_pending()) {
				a::CommandWorkBudget budget;
				budget.bytes = 512;
				budget.receipts = 1;
				pinned->collect(budget);
				check(++turns < 1000);
			}
			check(turns >= 256);
		}
		std::cout << "frozen authorized projection and cursor retention passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
