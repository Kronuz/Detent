#include "cluster_harness.hh"
#include "offline_runtime.hh"
#include "read_reply.hh"
#include <array>
#include <cassert>
#include <completion/posix.h>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <rapidjson/document.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
using namespace kronuz::consensus;
using detent::authority::testing::Cluster;
using detent::authority::testing::completion_storage;
int main(int argc, char **argv) {
	completion_storage = argc == 2 && std::string_view(argv[1]) == "--completion-storage";
	Cluster group;
	group.until([&] { return group.nodes[0]->worker().role() == Role::Leader; });
	Command open;
	open.context = group.context;
	open.kind = Kind::Open;
	open.producer = "writer";
	open.nonce[0] = 'n';
	assert(group.request(0, 1, open).results[0].generation == 1);
	group.link(0, 1, true);
	group.link(1, 2, true);
	auto initial = group.transaction(1, 1);
	std::vector<std::string> read_keys{"ns:key"};
	for (unsigned key = 0; key < 127; ++key) {
		detent::BoolDef definition{false, {}};
		for (unsigned group = 0; group < 31; ++group)
			definition.overrides.emplace("group" + std::to_string(group), true);
		auto name = "ns:held" + std::to_string(key);
		read_keys.push_back(name);
		initial.mutations.push_back({std::move(name), std::move(definition), {}, {}});
	}
	assert(group.request(0, 11, initial).results[0].status == Status::Applied);
	group.until([&] { return group.nodes[1]->state().revision() == 1; });
	// Pause an actual read capture across the follower's snapshot activation.
	std::optional<State> read_capture(group.nodes[1]->state());
	std::optional<ReadReplyEncoder> read_encoder;
	read_encoder.emplace(*read_capture, "ns", read_keys, group.context, 1024 * 1024);
	while (!read_encoder->counted()) {
		CommandWorkBudget budget;
		read_encoder->step(budget);
	}
	read_encoder->begin_encoding();
	CommandWorkBudget header;
	header.bytes = 1024;
	header.records = 1;
	read_encoder->step(header);
	assert(!read_encoder->done());
	group.link(0, 1, false);
	group.link(1, 2, false);
	for (unsigned sequence = 2; sequence <= 8; ++sequence) {
		assert(group.request(0, 10 + sequence, group.transaction(sequence, sequence)).results[0].status ==
			   Status::Applied);
	}
	group.until(
		[&] { return group.nodes[0]->worker().applied_index() == group.nodes[0]->state().applied_index(); });
	assert(group.nodes[0]->checkpoint());
	group.until([&] { return !group.nodes[0]->checkpoint_busy(); });
	assert(group.request(0, 30, group.transaction(9, 9)).results[0].status == Status::Applied);
	group.link(0, 1, true);
	group.link(1, 2, true);
	group.until(
		[&] { return group.nodes[1]->state().revision() == 9 && group.nodes[2]->state().revision() == 9; });
	assert(group.nodes[1]->worker().base_index() > 0 &&
		   group.nodes[1]->state().lookup("writer", 1, 8) == group.nodes[0]->state().lookup("writer", 1, 8));
	assert(!read_capture->shares_reclamation_with(group.nodes[1]->state()));
	while (!read_encoder->done()) {
		CommandWorkBudget budget;
		read_encoder->step(budget);
	}
	auto old_reply = read_encoder->take();
	rapidjson::Document frozen;
	frozen.Parse(old_reply.c_str());
	assert(!frozen.HasParseError() && frozen["version"].GetUint64() == 1 &&
		   frozen["namespace_revision"].GetUint64() == 1 &&
		   frozen["knobs"]["ns:key"]["default"].GetInt() == 1);
	read_encoder.reset();
	read_capture->retire();
	unsigned retirement_turns = 0;
	while (read_capture->reclamation_pending()) {
		CommandWorkBudget budget;
		budget.bytes = 2048;
		budget.receipts = 1;
		read_capture->collect(budget);
		assert(++retirement_turns < 50000);
	}
	assert(retirement_turns > 127 && read_capture->retained_nodes() == 0 &&
		   read_capture->retired_values() == 0);
	read_capture.reset();
	auto lost = group.request(0, 31, group.transaction(10, 10));
	auto revision = group.nodes[0]->state().revision();
	auto retry = group.request(0, 32, group.transaction(10, 10));
	assert(retry.results == lost.results && group.nodes[0]->state().revision() == revision);
	auto other = open;
	other.producer = "other";
	other.nonce[0] = 'o';
	assert(group.request(0, 33, other).results[0].generation == 1);
	group.until([&] {
		return group.nodes[1]->state().producer("other") && group.nodes[2]->state().producer("other");
	});
	group.link(0, 1, false);
	group.link(0, 2, false);
	bool submitted = false;
	group.until([&] {
		if (!submitted) {
			submitted = group.nodes[0]->propose(40, {group.transaction(11, 11)}) == SubmitResult::Accepted;
		}
		return submitted;
	});
	for (unsigned i = 0; i < 200; ++i) {
		group.step();
	}
	assert(group.nodes[0]->state().revision() == revision &&
		   std::none_of(group.completed[0].begin(), group.completed[0].end(),
						[](auto &result) { return result.request == 40; }));

	auto other_transaction = group.transaction(1, 123);
	other_transaction.producer = "other";
	other_transaction.mutations[0].key = "ns:other";
	bool other_submitted = false;
	group.until([&] {
		if (!other_submitted)
			other_submitted = group.nodes[0]->propose(43, {other_transaction}) == SubmitResult::Accepted;
		return other_submitted;
	});
	for (unsigned i = 0; i < 200; ++i)
		group.step();
	bool read_submitted = false;
	group.until([&] {
		if (!read_submitted)
			read_submitted = group.nodes[0]->read(42) == SubmitResult::Accepted;
		return read_submitted;
	});
	for (unsigned i = 0; i < 200; ++i)
		group.step();
	assert(std::none_of(group.completed[0].begin(), group.completed[0].end(), [](const auto &result) {
		return result.request == 42 && result.read && !result.rejection;
	}));
	group.nodes[0]->forget(42);
	group.until([&] {
		return group.nodes[1]->worker().role() == Role::Leader ||
			   group.nodes[2]->worker().role() == Role::Leader;
	});
	auto leader = group.nodes[1]->worker().role() == Role::Leader ? 1u : 2u;
	assert(group.request(leader, 41, group.transaction(11, 99)).results[0].revision == revision + 1);

	group.link(0, 1, true);
	group.link(0, 2, true);
	group.until([&] {
		return group.nodes[0]->state().revision() == revision + 1 &&
			   group.nodes[1]->state().revision() == revision + 1 &&
			   group.nodes[2]->state().revision() == revision + 1;
	});
	group.until([&] {
		return std::any_of(group.completed[0].begin(), group.completed[0].end(),
						   [](const auto &result) { return result.request == 43; });
	});
	auto stale = std::find_if(group.completed[0].begin(), group.completed[0].end(),
							  [](const auto &result) { return result.request == 43; });
	assert(stale->rejection && stale->results.empty());
	auto no_op = std::find_if(group.completed[0].begin(), group.completed[0].end(),
							  [](const auto &result) { return result.request == 40; });
	assert(no_op != group.completed[0].end() && no_op->rejection && no_op->results.empty());
	assert(!group.nodes[0]->state().find("ns:other"));
	group.link(0, 1, false);
	group.link(0, 2, false);
	group.nodes[0].reset();
	group.messages[0].clear();
	group.launch(0, true);
	group.link(0, 1, true);
	group.link(0, 2, true);
	group.until([&] { return group.nodes[0]->state().revision() == revision + 1; });
	for (auto &node : group.nodes) {
		assert(std::get<detent::IntDef>(*node->state().find("ns:key")->value).default_value == 99 &&
			   node->state().lookup("writer", 1, 10) == lost.results[0]);
	}
	group.until([&] {
		return std::all_of(group.nodes.begin(), group.nodes.end(), [](const auto &node) {
			return !node->worker().busy() && node->state().applied_index() == node->worker().committed();
		});
	});
	for (unsigned i = 0; i < group.nodes.size(); ++i) {
		auto expected = group.nodes[i]->state().snapshot();
		auto committed = group.nodes[i]->worker().committed();
		group.nodes[i].reset();
		ServiceOptions profile;
		profile.node = group.options(i);
		profile.directory = group.root / std::to_string(i);
		{
			OfflineRuntime offline(profile);
			offline.recover_committed();
			assert(offline.node.worker().role() == Role::Follower);
			assert(offline.node.worker().committed() == committed);
			assert(offline.node.state().snapshot().knobs == expected.knobs);
			offline.checkpoint();
			assert(offline.node.worker().base_index() == committed);
			assert(offline.node.worker().role() == Role::Follower);
		}
		OfflineRuntime restored(profile);
		restored.recover_committed();
		assert(restored.node.worker().base_index() == committed);
		assert(restored.node.state().snapshot().knobs == expected.knobs);
	}
	std::cout << "three-Worker authority snapshot, retry, failover and offline checkpoints passed\n";
}
