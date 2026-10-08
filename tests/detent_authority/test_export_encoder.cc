#include "detent_distribution/messagepack.hh"
#include "export_encoder.hh"
#include <iostream>
#include <limits>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace m = d::messagepack;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("snapshot export invariant");
}
std::string encode(a::SnapshotExportEncoder &encoder, std::size_t bytes = 1) {
	std::string result;
	std::size_t turns = 0;
	while (!encoder.done()) {
		d::WorkBudget budget{bytes, 1};
		encoder.step(budget);
		if (encoder.chunk_ready()) {
			auto chunk = encoder.take_chunk();
			result.append(chunk->view());
		}
		check(++turns < 10000000);
	}
	d::WorkBudget zero{0, 0};
	encoder.step(zero);
	return result;
}
std::string oracle(const d::Cursor &id) {
	std::string out;
	auto ar = [&](unsigned value) { m::array(out, value); };
	auto n = [&](std::uint64_t value) { m::unsigned_integer(out, value); };
	auto s = [&](std::string_view value) { m::string(out, value); };
	ar(7);
	n(1);
	n(1);
	ar(2);
	n(1);
	n(1);
	ar(5);
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, {field->data(), field->size()}, true);
	m::string(out, {id.scope.data(), id.scope.size()}, true);
	n(3);
	ar(4);
	n(2);
	n(2);
	n(2);
	n(30);
	ar(2);
	ar(2);
	s("app");
	n(1);
	ar(2);
	s("app2");
	n(2);
	ar(2);
	ar(4);
	n(0);
	s("app:flag");
	n(1);
	ar(4);
	n(0);
	out.push_back('\xc3');
	n(25);
	ar(2);
	ar(3);
	s("beta");
	n(1);
	n(17);
	ar(3);
	s("z");
	n(0);
	out.push_back('\xc2');
	ar(4);
	n(1);
	s("app2:count");
	n(2);
	ar(2);
	n(1);
	m::signed_integer(out, std::numeric_limits<std::int64_t>::min());
	return out;
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
		command.producer = "private-producer";
		command.kind = a::Kind::Open;
		command.nonce.fill('n');
		check(state.apply(1, command).generation == 1);
		command.kind = a::Kind::Transaction;
		command.nonce = {};
		command.generation = 1;
		std::uint64_t index = 1;
		auto put = [&](std::string ns, std::vector<a::Mutation> mutations) {
			command.name_space = std::move(ns);
			++command.sequence;
			command.mutations = std::move(mutations);
			check(state.apply(++index, command).status == a::Status::Applied);
		};
		detent::BoolDef flag;
		flag.default_value = true;
		flag.rollout_pct = 25;
		flag.overrides.emplace("beta", detent::GroupRollout{17});
		flag.overrides.emplace("z", false);
		put("app", {{"app:flag", flag, {}, {}}});
		put("app2", {{"app2:count", detent::IntDef{std::numeric_limits<std::int64_t>::min()}, {}, {}}});
		put("secret", {{"secret:private", detent::IntDef{999}, {}, {}}});
		auto account = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
		std::set<std::string> allowed{"app", "app2"};
		std::vector<std::string> names{"app2", "app"};
		auto scope = a::AuthorizedScope::authorize("reader", 1, names, allowed, account);
		auto capture = state.capture(scope);
		auto frozen = capture->cursor();
		{
			a::SnapshotExportEncoder encoder(capture);
			const auto before = account->used();
			d::WorkBudget zero{0, 0};
			encoder.step(zero);
			check(!encoder.chunk_ready() && account->used() == before);
			put("app", {{"app:flag", detent::IntDef{100}, {}, {}}});
			const auto bytes = encode(encoder);
			check(bytes == oracle(frozen) && encoder.encoded_length() == bytes.size());
			d::SnapshotDecoder decoder(frozen, {"app", "app2"});
			auto input = std::span<const char>(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget budget{1, 1};
				decoder.feed(input, budget);
			}
			auto decoded = decoder.finish();
			check(decoded.records().size() == 2 &&
				  decoded.overrides(std::get<d::WireBoolDef>(decoded.records()[0].definition)).size() == 2);
		}
		capture->retire();
		while (capture->reclamation_pending()) {
			a::CommandWorkBudget budget;
			capture->collect(budget);
		}
		capture.reset();
		// Force multiple output chunks and validate the frozen source under writes.
		for (unsigned batch = 0; batch < 6; ++batch) {
			std::vector<a::Mutation> mutations;
			for (unsigned i = 0; i < 128; ++i)
				mutations.push_back({"app:" + std::string(90, 'x') + std::to_string(batch * 128 + i),
									 detent::IntDef{i},
									 {},
									 {}});
			put("app", std::move(mutations));
		}
		capture = state.capture(scope);
		frozen = capture->cursor();
		std::shared_ptr<const d::OwnedBuffer> held;
		{
			a::SnapshotExportEncoder encoder(capture);
			std::string bytes;
			unsigned chunks = 0;
			while (!encoder.done()) {
				d::WorkBudget budget{8192, 16};
				encoder.step(budget);
				if (encoder.chunk_ready()) {
					auto chunk = encoder.take_chunk();
					bytes.append(chunk->view());
					++chunks;
					if (!held)
						held = chunk;
				}
			}
			check(chunks >= 2 && held->size() == 65536 && encoder.encoded_length() == bytes.size());
			d::SnapshotDecoder decoder(frozen, {"app", "app2"});
			auto input = std::span<const char>(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget budget;
				decoder.feed(input, budget);
			}
			auto decoded = decoder.finish();
			check(decoded.records().size() == 770);
		}
		capture->retire();
		while (capture->reclamation_pending()) {
			a::CommandWorkBudget budget;
			capture->collect(budget);
		}
		capture.reset();
		const auto retained = account->used();
		held.reset();
		check(account->used() < retained);
		capture = state.capture(scope);
		{
			a::SnapshotExportEncoder encoder(capture);
			while (!encoder.chunk_ready()) {
				d::WorkBudget budget;
				encoder.step(budget);
			}
			held = encoder.take_chunk();
			const auto before = account->used();
			bool refused = false;
			{
				auto pressure = account->admit(account->limit() - before);
				try {
					d::WorkBudget budget;
					encoder.step(budget);
				} catch (const d::StoragePressure &) {
					refused = true;
				}
				check(account->used() == account->limit());
			}
			check(refused && account->used() == before);
			encoder.discard();
			check(!encoder.done());
		}
		capture->retire();
		while (capture->reclamation_pending()) {
			a::CommandWorkBudget budget;
			capture->collect(budget);
		}
		capture.reset();

		capture = state.capture(scope);
		for (unsigned rejected_limit = 0; rejected_limit < 5; ++rejected_limit) {
			const auto before = account->used();
			bool rejected = false;
			try {
				d::Limits limits;
				if (rejected_limit == 0)
					limits.namespaces = 1;
				if (rejected_limit == 1)
					limits.records = 1;
				if (rejected_limit == 2)
					limits.string_bytes = 1;
				if (rejected_limit == 3)
					limits.definition_bytes = 1;
				if (rejected_limit == 4)
					limits.bytes = 1;
				a::SnapshotExportEncoder encoder(capture, limits);
				try {
					while (!encoder.done()) {
						d::WorkBudget budget;
						encoder.step(budget);
						check(!encoder.chunk_ready());
					}
				} catch (...) {
					encoder.discard();
					throw;
				}
			} catch (const std::length_error &) {
				rejected = true;
			}
			check(rejected && account->used() == before);
		}
		capture->retire();
		while (capture->reclamation_pending()) {
			a::CommandWorkBudget budget;
			capture->collect(budget);
		}
		capture.reset();
		{
			std::vector<std::string> none;
			auto empty_scope = a::AuthorizedScope::authorize("reader", 1, none, allowed, account);
			auto empty_capture = state.capture(empty_scope);
			a::SnapshotExportEncoder encoder(empty_capture);
			auto bytes = encode(encoder);
			d::SnapshotDecoder decoder(empty_capture->cursor(), {});
			auto input = std::span<const char>(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget budget;
				decoder.feed(input, budget);
			}
			auto decoded = decoder.finish();
			check(decoded.records().empty() && decoded.namespaces().empty());
			encoder.discard();
			empty_capture->retire();
			while (empty_capture->reclamation_pending()) {
				a::CommandWorkBudget budget;
				empty_capture->collect(budget);
			}
		}
		held.reset();
		scope.reset();
		check(account->used() == 0);
		std::cout << "snapshot export oracle, multi-chunk framing and retained admission passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
