#include "node.hh"
#include "prepared.hh"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <journal/posix.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
using namespace cluster::consensus;
namespace {
bool completion_storage = false;
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
NodeOptions options() {
	NodeOptions o;
	o.scheduling.async_storage = completion_storage;
	o.context = context();
	o.configuration.cluster = o.context.cluster;
	o.configuration.configuration[0] = 'v';
	o.configuration.local = 1;
	o.configuration.voters = {1};
	o.consensus.log_bytes = 4 * 1024 * 1024;
	o.consensus.log_entries = 128;
	o.application.producers = 16;
	o.application.namespaces = 64;
	o.application.image_bytes = 4 * 1024 * 1024;
	o.application.state_bytes = 2 * 1024 * 1024;
	return o;
}
Command open() {
	Command c;
	c.context = context();
	c.kind = Kind::Open;
	c.producer = "p";
	c.nonce[0] = 'n';
	return c;
}
Command transaction(std::uint64_t sequence, int value) {
	Command c;
	c.context = context();
	c.producer = "p";
	c.generation = 1;
	c.sequence = sequence;
	c.name_space = "ns";
	c.mutations.push_back({"ns:key", detent::IntDef{value}, {}, {}});
	return c;
}
std::vector<Completion> pump(Node &node, unsigned limit = 200) {
	std::vector<Completion> completed;
	static std::uint64_t clock = 0;
	for (unsigned i = 0; i < limit; ++i) {
		node.service({clock += 10, 100});
		if (auto job = node.worker().take_io_operation()) {
			kronuz::journal::drive_synchronously(*job);
		}
		auto actions = node.take_network_actions();
		for (const auto &action : actions) {
			assert(!std::holds_alternative<Fenced>(action) && !std::holds_alternative<Send>(action));
		}
		auto results = node.take_completions();
		for (auto &result : results) {
			completed.push_back(std::move(result));
		}
		assert(!node.worker().fenced());
	}
	return completed;
}
void submit(Node &node, std::uint64_t request, const Command &c) {
	for (unsigned i = 0; i < 200; ++i) {
		auto result = node.propose(request, {c});
		if (result == SubmitResult::Accepted) {
			return;
		}
		assert(result == SubmitResult::Busy || result == SubmitResult::Pressure);
		pump(node, 1);
	}
	assert(false);
}
void retained_application_range(const std::filesystem::path &directory, bool invalid_middle,
								bool grouped = false, bool backlog = false) {
	struct Cleanup {
		std::filesystem::path directory;
		~Cleanup() { std::filesystem::remove_all(directory); }
	} cleanup{directory};
	std::filesystem::create_directories(directory);
	assert(::chmod(directory.c_str(), 0700) == 0);
	auto profile = options();
	if (backlog) {
		profile.consensus.log_entries = 64;
		profile.consensus.rpc_entries = 4;
	}
	const std::uint64_t through = invalid_middle ? 5 : (grouped ? 3 : 32);
	{
		kronuz::journal::PosixIO io(directory);
		kronuz::journal::Journal journal(io, 4 * 1024 * 1024);
		kronuz::journal::Identity identity{};
		identity[0] = 'r';
		journal.create(identity);
		journal.append_batch(encode_initialization(profile.configuration));
		std::vector<Entry> entries{{1, 1, EntryKind::NoOp, {}}};
		entries.push_back({2, 1, EntryKind::Command,
						   encode_batch({open()}, profile.application, profile.consensus.command_bytes)});
		for (std::uint64_t index = 3; index <= through; ++index) {
			auto bytes = invalid_middle && index == 4
							 ? std::string("invalid application entry")
							 : encode_batch({transaction(index - 2, static_cast<int>(index))},
											profile.application, profile.consensus.command_bytes);
			if (grouped) {
				std::vector<Command> commands;
				for (unsigned sequence = 1; sequence <= 32; ++sequence)
					commands.push_back(transaction(sequence, static_cast<int>(sequence)));
				bytes = encode_batch(commands, profile.application, profile.consensus.command_bytes);
			}
			entries.push_back({index, 1, EntryKind::Command, std::move(bytes)});
		}
		if (backlog) {
			journal.append_batch(encode_storage_batch({HardState{1, 1, 0}, {}}));
			for (std::size_t offset = 0; offset < entries.size(); offset += profile.consensus.rpc_entries) {
				auto end = std::min(entries.size(), offset + profile.consensus.rpc_entries);
				std::vector<Entry> chunk(entries.begin() + offset, entries.begin() + end);
				journal.append_batch(
					encode_storage_batch({{}, LogMutation{chunk.front().index, std::move(chunk)}}));
			}
			journal.append_batch(encode_storage_batch({HardState{1, 1, through}, {}}));
		} else {
			journal.append_batch(
				encode_storage_batch({HardState{1, 1, through}, LogMutation{1, std::move(entries)}}));
		}
	}
	Node node(std::make_unique<kronuz::journal::PosixIO>(directory), profile);
	node.recover();
	while (!node.worker().ready()) {
		node.service({0, 100});
		if (auto job = node.worker().take_io_operation())
			kronuz::journal::drive_synchronously(*job);
		node.take_network_actions();
	}
	node.start();
	assert(node.state().applied_index() == 0);
	unsigned unpublished_turns = 0;
	bool early_capture = false;
	for (unsigned turn = 0; turn < 128 && !node.worker().fenced(); ++turn) {
		auto before = node.state().applied_index();
		// read() calls drain(), but cannot advance the application slot.
		(void)node.read(1000 + turn);
		assert(node.state().applied_index() == before);
		node.service({invalid_middle || backlog ? 0u : 1000u, 100});
		if (auto job = node.worker().take_io_operation())
			kronuz::journal::drive_synchronously(*job);
		assert(node.state().applied_index() <= before + 1);
		early_capture |= node.checkpoint_busy() && node.state().applied_index() < through;
		if (grouped && before == 2 && node.state().applied_index() == 2) {
			++unpublished_turns;
			assert(node.state().revision() == 0 && !node.state().find("ns:key"));
			assert(node.state().lookup("p", 1, 1).status == Status::SequenceGap);
		}

		// Retain host output for ownership tests; backlog must permit new ranges.
		if (backlog)
			node.take_network_actions();
		if (node.state().applied_index() >= through)
			break;
	}
	if (invalid_middle) {
		assert(node.worker().fenced());
		assert(node.state().applied_index() == 3 && node.state().revision() == 1);
	} else {
		assert(!node.worker().fenced() && node.state().applied_index() == through);
		assert(node.state().revision() == (grouped ? 32 : through - 2));
		if (grouped)
			assert(unpublished_turns >= 3);
		node.take_network_actions();
		if (backlog) {
			assert(early_capture);
			assert(node.checkpoint_policy().captured_boundary > 0 &&
				   node.checkpoint_policy().captured_boundary < through);
			pump(node, 300);
			assert(node.worker().base_index() > 0 && node.checkpoint_policy().published_boundary > 0);
		} else
			pump(node, 20);
		assert(node.worker().applied_index() >= through);
	}
}
} // namespace
void terminal_state_retirement(const std::filesystem::path &path) {
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{path};
	std::filesystem::create_directories(path);
	chmod(path.c_str(), 0700);
	Node node(std::make_unique<kronuz::journal::PosixIO>(path), options());
	Identity id{};
	id[0] = 'T';
	node.create(id);
	pump(node);
	node.start();
	pump(node);
	submit(node, 1, open());
	pump(node);
	auto command = transaction(1, 42);
	command.mutations.clear();
	for (unsigned key = 0; key < 128; ++key) {
		detent::BoolDef value{false, {}};
		for (unsigned group = 0; group < 31; ++group)
			value.overrides.emplace("group" + std::to_string(group), true);
		command.mutations.push_back({"ns:k" + std::to_string(key), std::move(value), {}, {}});
	}
	submit(node, 2, command);
	auto results = pump(node, 5000);
	assert(results.size() == 1 && results[0].results[0].status == Status::Applied &&
		   node.state().size() == 128);
	auto revision = node.state().revision();
	node.close();
	node.close();
	assert(node.read(3) == SubmitResult::Busy && node.propose(3, {command}) == SubmitResult::Busy);
	unsigned turns = 0;
	while (!node.drained()) {
		CommandWorkBudget budget;
		budget.bytes = 2048;
		budget.receipts = 1;
		node.retirement_step(budget);
		if (auto original = node.worker().take_io_operation())
			kronuz::journal::drive_synchronously(*original);
		assert(node.state().revision() == revision && ++turns < 50000);
	}
	assert(turns > 128 && node.state().size() == 0 && node.state().retained_nodes() == 0 &&
		   node.state().retired_values() == 0 && node.worker().retired_log_entries() == 0);
	CommandWorkBudget budget;
	node.retirement_step(budget);
	assert(node.drained());
}

int main(int argc, char **argv) {
	completion_storage = argc == 2 && std::string_view(argv[1]) == "--completion-storage";
	auto path = std::filesystem::current_path() / ".scratch" / ("authority-node-" + std::to_string(getpid()));
	std::filesystem::create_directories(path);
	chmod(path.c_str(), 0700);
	terminal_state_retirement(path / "terminal-state");
	retained_application_range(path / "retained-application", false);
	retained_application_range(path / "failed-application", true);
	retained_application_range(path / "grouped-application", false, true);
	retained_application_range(path / "backlogged-application", false, false, true);
	auto prepare = [](const Command &command) {
		CommandEncoder encoder(command, options().application);
		while (!encoder.done()) {
			CommandWorkBudget b;
			encoder.step(b);
		}
		auto encoded = encoder.take_encoded();
		ProposalBuilder builder(command.context, options().application);
		assert(builder.append(encoded));
		while (builder.command_pending()) {
			CommandWorkBudget b;
			builder.step(b);
		}
		builder.seal();
		while (!builder.done()) {
			CommandWorkBudget b;
			builder.step(b);
		}
		return builder.take();
	};
	auto submit_prepared = [](Node &node, std::uint64_t request, PreparedProposal &proposal) {
		const auto bytes = proposal.bytes();
		const auto digest = proposal.digest();
		for (unsigned attempt = 0; attempt < 200; ++attempt) {
			auto result = node.propose_prepared(request, proposal);
			if (result == SubmitResult::Accepted)
				return;
			assert(result == SubmitResult::Busy || result == SubmitResult::Pressure);
			assert(proposal.valid() && proposal.bytes() == bytes && proposal.digest() == digest);
			pump(node, 1);
		}
		assert(false);
	};
	std::uint64_t revision, checkpoint_index;
	{
		auto profile = options();
		profile.waiters = 1;
		Node node(std::make_unique<kronuz::journal::PosixIO>(path), profile);
		Identity id{};
		id[0] = 's';
		node.create(id);
		for (unsigned retry = 0; retry < 8; ++retry)
			assert(node.read(999) == SubmitResult::Busy);
		auto prepared_open = prepare(open());
		auto original_bytes = prepared_open.bytes();
		auto original_digest = prepared_open.digest();
		for (unsigned retry = 0; retry < 8; ++retry) {
			assert(node.propose_prepared(1, prepared_open) == SubmitResult::Busy);
			assert(prepared_open.valid() && prepared_open.bytes() == original_bytes &&
				   prepared_open.digest() == original_digest);
		}
		pump(node);
		node.start();
		pump(node);
		assert(node.worker().role() == Role::Leader);
		submit_prepared(node, 1, prepared_open);
		assert(!prepared_open.valid());
		auto prepared_change = prepare(transaction(1, 10));
		original_bytes = prepared_change.bytes();
		original_digest = prepared_change.digest();
		for (unsigned retry = 0; retry < 8; ++retry) {
			assert(node.propose_prepared(2, prepared_change) == SubmitResult::Pressure);
			assert(prepared_change.valid() && prepared_change.bytes() == original_bytes &&
				   prepared_change.digest() == original_digest);
		}
		auto opened = pump(node);
		assert(opened.size() == 1 && opened[0].results[0].generation == 1);
		submit_prepared(node, 2, prepared_change);
		assert(!prepared_change.valid());
		auto changed = pump(node);
		assert(changed.size() == 1 && changed[0].results[0].status == Status::Applied);
		revision = node.state().revision();
		assert(node.checkpoint());
		pump(node);
		assert(!node.checkpoint_busy());
		checkpoint_index = node.worker().base_index();
		assert(checkpoint_index == node.state().applied_index());
		submit(node, 3, transaction(2, 20));
		auto tail = pump(node);
		assert(tail.size() == 1 && node.state().revision() == revision + 1);
	}
	{
		Node node(std::make_unique<kronuz::journal::PosixIO>(path), options());
		node.recover();
		pump(node);
		node.start();
		pump(node);
		assert(node.state().revision() == revision + 1 && node.worker().base_index() == checkpoint_index);
		assert(std::get<detent::IntDef>(*node.state().find("ns:key")->value).default_value == 20);
		submit(node, 4, transaction(1, 10));
		auto retried = pump(node);
		assert(retried.size() == 1 && retried[0].results[0].revision == revision &&
			   node.state().revision() == revision + 1);
		assert(node.read(5) == SubmitResult::Accepted);
		auto read = pump(node);
		assert(read.size() == 1 && read[0].read && read[0].index <= node.state().applied_index());
	}
	auto plain = path.parent_path() / ("authority-profile-" + std::to_string(getpid()));
	std::filesystem::create_directories(plain);
	chmod(plain.c_str(), 0700);
	{
		Node node(std::make_unique<kronuz::journal::PosixIO>(plain), options());
		Identity id{};
		id[0] = 'p';
		node.create(id);
		pump(node);
		node.start();
		pump(node);
		submit(node, 1, open());
		pump(node);
		auto command = transaction(1, 1);
		command.mutations.push_back({"ns:second", detent::IntDef{2}, {}, {}});
		submit(node, 2, command);
		auto result = pump(node);
		assert(result.size() == 1 && result[0].results[0].status == Status::Applied &&
			   node.worker().base_index() == 0);
	}
	for (bool change_keys : {false, true}) {
		auto profile = options();
		if (change_keys)
			profile.application.keys = 1;
		else
			profile.application.results = 1;
		Node node(std::make_unique<kronuz::journal::PosixIO>(plain), profile);
		node.recover();
		for (unsigned i = 0; i < 1000 && !node.worker().ready(); ++i) {
			node.service({i, 100});
			if (auto job = node.worker().take_io_operation()) {
				kronuz::journal::drive_synchronously(*job);
			}
			node.take_network_actions();
		}
		assert(node.worker().ready());
		node.start();
		for (unsigned i = 0; i < 1000 && !node.worker().fenced(); ++i) {
			node.service({i, 100});
			if (auto job = node.worker().take_io_operation()) {
				kronuz::journal::drive_synchronously(*job);
			}
			node.take_network_actions();
		}
		assert(node.worker().fenced() && node.state().size() == 0);
	}
	std::filesystem::remove_all(plain);
	std::filesystem::remove_all(path);
	std::cout << "owned authority checkpoint/replay passed\n";
}
