#include "state.hh"
#include <iostream>
namespace a = detent::authority;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("optional change recorder invariant");
}
struct Draft final : a::AppliedChangeDraft {
	const a::State *candidate;
	std::uint64_t revision;
	unsigned turns = 0, disposal = 0;
	bool fail;
	std::vector<std::string> keys;
	Draft(const a::State &state, const a::Command &command, std::span<const unsigned char> changed,
		  bool fault)
		: candidate(&state), revision(state.revision()), fail(fault) {
		for (std::size_t i = 0; i < changed.size(); ++i)
			if (changed[i])
				keys.push_back(command.mutations[i].key);
	}
	bool step(a::CommandWorkBudget &budget) override {
		if (!budget.records)
			return false;
		--budget.records;
		++turns;
		check(candidate && candidate->revision() == revision);
		if (fail)
			throw std::bad_alloc();
		if (turns < 2)
			return false;
		candidate = nullptr;
		return true;
	}
	bool discard(a::CommandWorkBudget &budget) noexcept override {
		if (!budget.receipts)
			return false;
		--budget.receipts;
		if (++disposal < 2)
			return false;
		candidate = nullptr;
		return true;
	}
};
struct Recorder final : a::AppliedChangeRecorder {
	unsigned captures = 0, published = 0, abandoned = 0;
	std::uint64_t last_gap = 0;
	bool fail_capture = false, fail_step = false;
	std::vector<std::shared_ptr<Draft>> staged;
	Recorder() { staged.reserve(64); }
	void begin_batch(const a::Context &, std::uint64_t, std::uint64_t) noexcept override { staged.clear(); }
	std::shared_ptr<a::AppliedChangeDraft> capture(const a::State &candidate, const a::Command &command,
												   std::span<const unsigned char> changed) override {
		++captures;
		if (fail_capture)
			throw std::bad_alloc();
		return std::make_shared<Draft>(candidate, command, changed, fail_step);
	}
	void stage(std::shared_ptr<a::AppliedChangeDraft> draft) noexcept override {
		staged.push_back(std::static_pointer_cast<Draft>(std::move(draft)));
	}
	void gap(std::uint64_t revision) noexcept override { last_gap = revision; }
	void publish(const a::Context &, std::uint64_t, std::uint64_t) noexcept override { ++published; }
	void abandon_batch() noexcept override {
		++abandoned;
		staged.clear();
	}
	void reset(const a::Context &, std::uint64_t) noexcept override { staged.clear(); }
	bool pending() const noexcept override { return false; }
	void collect(a::CommandWorkBudget &) noexcept override {}
};
a::ValidatedCommand validated(const a::Command &command) {
	auto bytes = a::encode(command);
	a::CommandDecoder decoder(bytes);
	while (!decoder.done()) {
		a::CommandWorkBudget budget;
		decoder.step(budget);
	}
	return decoder.take();
}
} // namespace
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
		command.name_space = "ns";
		auto recorder = std::make_shared<Recorder>();
		std::uint64_t index = 1;
		auto apply = [&](const a::Command &input) {
			const auto before = state.revision();
			recorder->begin_batch(context, index + 1, before);
			a::BatchApplication batch(state, ++index, 1, 1, recorder);
			batch.start(validated(input));
			unsigned turns = 0;
			while (!batch.done()) {
				a::CommandWorkBudget budget;
				batch.step(budget);
				check(++turns < 10000 && state.revision() == before);
			}
			auto result = batch.finish();
			for (const auto &draft : recorder->staged)
				check(!draft->candidate && draft->turns >= 2);
			state = std::move(result.state);
			recorder->publish(context, index, state.revision());
			return result.results.front();
		};
		command.sequence = 1;
		command.mutations = {{"ns:a", detent::IntDef{1}, {}, {}}};
		check(apply(command).status == a::Status::Applied && recorder->captures == 1 &&
			  recorder->staged[0]->keys == std::vector<std::string>{"ns:a"});
		check(apply(command).status == a::Status::Applied && recorder->captures == 1 &&
			  recorder->staged.empty());
		command.sequence = 2;
		command.mutations = {{"ns:a", detent::IntDef{1}, {}, {}}, {"ns:b", detent::IntDef{2}, {}, {}}};
		check(apply(command).status == a::Status::Applied && recorder->captures == 2 &&
			  recorder->staged[0]->keys == std::vector<std::string>{"ns:b"});
		command.sequence = 3;
		command.mutations = {{"ns:a", {}, {}, {}}, {"ns:b", detent::IntDef{2}, {}, {}}};
		check(apply(command).status == a::Status::Applied && recorder->captures == 3 &&
			  recorder->staged[0]->keys == std::vector<std::string>{"ns:a"} && !state.find("ns:a"));
		recorder->fail_capture = true;
		command.sequence = 4;
		command.mutations = {{"ns:b", detent::IntDef{4}, {}, {}}};
		check(apply(command).status == a::Status::Applied && state.revision() == 4 &&
			  recorder->last_gap == 4 && recorder->staged.empty());
		recorder->fail_capture = false;
		recorder->fail_step = true;
		command.sequence = 5;
		command.mutations = {{"ns:b", detent::IntDef{5}, {}, {}}};
		check(apply(command).status == a::Status::Applied && state.revision() == 5 &&
			  recorder->last_gap == 5 && recorder->staged.empty());
		recorder->fail_step = false;
		command.sequence = 6;
		command.mutations = {{"ns:b", detent::IntDef{6}, {}, {}}};
		recorder->begin_batch(context, index + 1, state.revision());
		a::BatchApplication partial(state, ++index, 1, 1, recorder);
		partial.start(validated(command));
		const auto captures = recorder->captures;
		while (recorder->captures == captures) {
			a::CommandWorkBudget budget;
			budget.records = 1;
			partial.step(budget);
		}
		while (true) {
			a::CommandWorkBudget budget;
			if (partial.discard(budget))
				break;
		}
		recorder->abandon_batch();
		check(state.revision() == 5 && recorder->staged.empty());
		std::cout << "changed-only capture, retry filtering, optional refusal and cancellation passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
