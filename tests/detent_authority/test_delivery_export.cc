#include "delivery_export.hh"
#include <iostream>
#include <source_location>
namespace a = detent::authority;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("delivery export invariant at " + std::to_string(where.line()));
}
std::string receive(a::DeliveryExport &exporter, bool compress, std::size_t quantum) {
	std::string result;
	std::optional<d::FragmentDecoder> decoder;
	unsigned turns = 0;
	while (!exporter.done()) {
		d::WorkBudget work{quantum, 1};
		exporter.step(work);
		if (exporter.frame_ready()) {
			if (!decoder)
				decoder.emplace(exporter.kind(), exporter.encoded_length(), compress);
			auto frame = exporter.take_frame();
			auto input = std::span<const char>(frame->data(), frame->size());
			while (!input.empty()) {
				d::WorkBudget read{17, 1};
				decoder->feed(input, read);
				if (auto decoded = decoder->take())
					result.append(decoded->bytes());
			}
		}
		check(++turns < 10000000);
	}
	decoder->finish();
	check(result.size() == exporter.encoded_length());
	return result;
}
void dispose(a::DeliveryExport &exporter) {
	unsigned turns = 0;
	for (;;) {
		d::WorkBudget wire{8192, 4};
		a::CommandWorkBudget roots;
		if (exporter.discard(wire, roots))
			return;
		check(++turns < 1000000);
	}
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
		auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
		auto history = std::make_shared<a::ChangeHistory>(account);
		history->reset(context, 0);
		const std::array<unsigned char, 1> changed{1};
		for (unsigned revision = 1; revision <= 3; ++revision) {
			command.sequence = revision;
			command.name_space = revision == 2 ? "secret" : "ns";
			command.mutations = {{revision == 2 ? "secret:x" : "ns:a", detent::IntDef{revision}, {}, {}}};
			history->begin_batch(context, revision + 1, state.revision());
			check(state.apply(revision + 1, command).status == a::Status::Applied);
			auto draft = history->capture(state, command, changed);
			for (;;) {
				a::CommandWorkBudget work;
				if (draft->step(work))
					break;
			}
			history->stage(draft);
			history->publish(context, revision + 1, state.revision());
		}
		auto scope =
			a::AuthorizedScope::authorize("reader", 1, std::vector<std::string>{"ns"}, {"ns"}, account);
		d::Identity update;
		update.fill('u');
		d::Cursor base{context.cluster, context.group, context.epoch, scope->identity(), 0};
		for (bool compressed : {false, true}) {
			for (unsigned scenario = 0; scenario < 4; ++scenario) {
				auto from = base;
				if (scenario == 2)
					from.revision = 3; // Equal cursor must send state for conflict checking.
				if (scenario == 3)
					from.scope.fill('f');
				a::DeliveryExport exporter(state, scope, history,
										   scenario == 0 ? std::nullopt : std::optional{from}, update,
										   compressed);
				const auto handoff = exporter.history_handoff();
				check(handoff && handoff->revision() == exporter.target().revision);
				check(history->continuity(*handoff, state.revision()) == a::ChangeHistory::Continuity::Ready);
				d::WorkBudget zero{0, 0};
				exporter.step(zero);
				check(!exporter.counted());
				auto bytes = receive(exporter, compressed, 1);
				if (scenario == 1) {
					check(exporter.kind() == d::UpdateKind::Delta);
					d::DeltaDecoder decoder(from, {"ns"});
					auto input = std::span<const char>(bytes.data(), bytes.size());
					while (!input.empty()) {
						d::WorkBudget work;
						decoder.feed(input, work);
					}
					auto delta = decoder.finish();
					check(delta.transactions().size() == 2);
				} else {
					check(exporter.kind() == d::UpdateKind::Snapshot);
					d::SnapshotDecoder decoder(exporter.target(), {"ns"});
					auto input = std::span<const char>(bytes.data(), bytes.size());
					while (!input.empty()) {
						d::WorkBudget work;
						decoder.feed(input, work);
					}
					auto snapshot = decoder.finish();
					check(snapshot.records().size() == 1);
				}
				dispose(exporter);
			}
		}
		for (auto selection : {a::ExportSelection::SnapshotOnly, a::ExportSelection::ResumeOnly}) {
			a::DeliveryExport exporter(state, scope, history, base, update, false, {}, selection,
									   history->handoff(context, 0));
			auto bytes = receive(exporter, false, 1);
			check(!bytes.empty() &&
				  exporter.kind() == (selection == a::ExportSelection::SnapshotOnly ? d::UpdateKind::Snapshot
																					: d::UpdateKind::Delta));
			dispose(exporter);
		}
		{
			d::Limits bounds;
			bounds.records = 1;
			a::DeliveryExport exporter(state, scope, history, base, update, false, bounds,
									   a::ExportSelection::ResumeOnly, history->handoff(context, 0));
			bool rejected = false;
			try {
				receive(exporter, false, 1);
			} catch (const a::SnapshotRequired &error) {
				rejected = error.reason() == a::SnapshotRequired::Reason::RepresentationLimit;
			}
			check(rejected && !exporter.counted() && !exporter.frame_ready());
			dispose(exporter);
		}
		{
			a::DeliveryExport exporter(state, scope, history, base, update, false, {},
									   a::ExportSelection::ResumeOnly, history->handoff(context, 0));
			auto refusal = account->admit(account->limit() - account->used());
			bool pressured = false;
			try {
				d::WorkBudget work;
				exporter.step(work);
			} catch (const d::StoragePressure &) {
				pressured = true;
			}
			check(pressured && !exporter.counted());
			refusal.reset();
			dispose(exporter);
		}
		// A slow snapshot stays frozen while committed writes extend its handoff.
		{
			a::DeliveryExport exporter(state, scope, history, {}, update, false);
			const auto handoff = exporter.history_handoff();
			check(handoff && handoff->revision() == 3);
			d::WorkBudget tiny{1, 1};
			exporter.step(tiny);
			check(!exporter.counted());
			command.sequence = 4;
			command.name_space = "ns";
			command.mutations = {{"ns:a", detent::IntDef{44}, {}, {}}};
			history->begin_batch(context, 5, state.revision());
			check(state.apply(5, command).status == a::Status::Applied);
			auto draft = history->capture(state, command, changed);
			for (;;) {
				a::CommandWorkBudget work;
				if (draft->step(work))
					break;
			}
			history->stage(draft);
			history->publish(context, 5, state.revision());
			check(state.revision() == 4 && exporter.target().revision == 3);
			check(history->continuity(*handoff, 4) == a::ChangeHistory::Continuity::Ready);
			auto bytes = receive(exporter, false, 1);
			d::SnapshotDecoder decoder(exporter.target(), {"ns"});
			auto input = std::span<const char>(bytes.data(), bytes.size());
			while (!input.empty()) {
				d::WorkBudget work;
				decoder.feed(input, work);
			}
			auto snapshot = decoder.finish();
			check(snapshot.cursor().revision == 3 && snapshot.records().size() == 1);
			check(snapshot.records()[0].key == "ns:a" && snapshot.records()[0].revision == 3 &&
				  std::get<detent::IntDef>(snapshot.records()[0].definition).default_value == 3);
			dispose(exporter);
		}
		// Revoke coverage after admission, before count: fallback is owner-local.
		{
			a::DeliveryExport strict(state, scope, history, base, update, false, {},
									 a::ExportSelection::ResumeOnly, history->handoff(context, 0));
			a::DeliveryExport exporter(state, scope, history, base, update, false);
			const auto handoff = exporter.history_handoff();
			check(handoff.has_value());
			history->reset(context, state.revision());
			check(history->continuity(*handoff, state.revision()) ==
				  a::ChangeHistory::Continuity::HistoryLost);
			bool reset = false;
			try {
				receive(strict, false, 1);
			} catch (const a::SnapshotRequired &error) {
				reset = error.reason() == a::SnapshotRequired::Reason::HistoryLost;
			}
			check(reset && !strict.counted() && !strict.frame_ready());
			dispose(strict);
			auto bytes = receive(exporter, false, 17);
			check(exporter.kind() == d::UpdateKind::Snapshot);
			dispose(exporter);
		}
		// A large export spans frames; zero-budget cancellation retains its input.
		for (unsigned row = 0; row < 4000; ++row) {
			command.sequence++;
			command.name_space = "ns";
			command.mutations = {{"ns:key" + std::to_string(row), detent::IntDef{row}, {}, {}}};
			check(state.apply(row + 6, command).status == a::Status::Applied);
		}
		{
			a::DeliveryExport exporter(state, scope, history, {}, update, true);
			auto bytes = receive(exporter, true, 127);
			check(bytes.size() > d::fragment_bytes);
			dispose(exporter);
		}
		{
			a::DeliveryExport exporter(state, scope, history, {}, update, true);
			for (unsigned turn = 0; turn < 32; ++turn) {
				d::WorkBudget work{512, 2};
				exporter.step(work);
			}
			const auto held = account->used();
			d::WorkBudget wire{0, 0};
			a::CommandWorkBudget roots{0, 0, 0, 0};
			check(!exporter.discard(wire, roots) && account->used() == held);
			dispose(exporter);
		}
		history.reset();
		scope.reset();
		check(account->used() == 0);
		std::cout << "delivery export passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
