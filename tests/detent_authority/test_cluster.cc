#include "node.hh"
#include <array>
#include <cassert>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <journal/posix.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
using namespace cluster::consensus;
namespace {
bool completion_storage = false;
struct Cluster {
	std::filesystem::path root;
	std::array<std::unique_ptr<Node>, 3> nodes;
	std::array<std::deque<Receive>, 3> messages;
	std::array<std::vector<Completion>, 3> completed;
	bool connected[3][3]{};
	std::uint64_t clock = 0;
	unsigned connection = 0;
	Context context;
	Cluster() {
		context.cluster[0] = 'c';
		context.group[0] = 'g';
		context.epoch[0] = 'e';
		root =
			std::filesystem::current_path() / ".scratch" / ("authority-cluster-" + std::to_string(getpid()));
		std::filesystem::create_directories(root);
		for (unsigned i = 0; i < 3; ++i) {
			launch(i, false);
		}
		link(0, 2, true);
	}
	~Cluster() {
		nodes = {};
		std::filesystem::remove_all(root);
	}
	NodeOptions options(unsigned i) {
		NodeOptions o;
		o.scheduling.async_storage = completion_storage;
		o.context = context;
		o.configuration.cluster = context.cluster;
		o.configuration.configuration[0] = 'v';
		o.configuration.local = i + 1;
		o.configuration.voters = {1, 2, 3};
		o.consensus.log_bytes = 8 * 1024 * 1024;
		o.consensus.log_entries = 128;
		o.application.producers = 16;
		o.application.namespaces = 64;
		o.application.results = 4;
		o.application.state_bytes = 2 * 1024 * 1024;
		o.application.image_bytes = 4 * 1024 * 1024;
		o.timing.heartbeat = 100;
		o.timing.rpc_timeout = 200;
		o.timing.election_min = 1000;
		o.timing.election_max = 2000;
		return o;
	}
	void launch(unsigned i, bool recover) {
		auto directory = root / std::to_string(i);
		std::filesystem::create_directories(directory);
		chmod(directory.c_str(), 0700);
		nodes[i] = std::make_unique<Node>(std::make_unique<kronuz::journal::PosixIO>(directory), options(i));
		if (recover) {
			nodes[i]->recover();
		} else {
			Identity id{};
			id[0] = static_cast<char>('a' + i);
			nodes[i]->create(id);
		}
		for (unsigned t = 0; !nodes[i]->worker().ready() && t < 1000; ++t) {
			nodes[i]->service({clock, 1000 + i * 500});
			if (auto job = nodes[i]->worker().take_io_operation()) {
				kronuz::journal::drive_synchronously(*job);
			}
			nodes[i]->take_network_actions();
		}
		assert(nodes[i]->worker().ready());
		nodes[i]->start();
	}
	void link(unsigned a, unsigned b, bool enabled) {
		connected[a][b] = connected[b][a] = enabled;
		++connection;
		if (!enabled) {
			for (auto [i, peer] : {std::pair{a, b}, std::pair{b, a}}) {
				if (!nodes[i]) {
					continue;
				}
				Identity local{}, remote{}; // close the exact registered session below via our saved table
				(void)local;
				(void)remote;
				auto session = sessions[i][peer];
				if (session) {
					nodes[i]->channel().session_closed(*session);
				}
				sessions[i][peer].reset();
			}
			return;
		}
		Identity first{}, second{};
		first[0] = static_cast<char>(a + 1);
		second[0] = static_cast<char>(b + 1);
		first[1] = second[1] = static_cast<char>(connection);
		sessions[a][b] = TrustedSession{b + 1, first, second};
		sessions[b][a] = TrustedSession{a + 1, second, first};
		nodes[a]->channel().register_session(*sessions[a][b]);
		nodes[b]->channel().register_session(*sessions[b][a]);
	}
	std::optional<TrustedSession> sessions[3][3];
	void step() {
		++clock;
		for (unsigned i = 0; i < 3; ++i) {
			if (!nodes[i]) {
				continue;
			}
			if (!messages[i].empty()) {
				auto event = messages[i].front();
				auto source = static_cast<unsigned>(event.authenticated_peer - 1);
				if (!connected[i][source] || !nodes[source]) {
					messages[i].pop_front();
				} else {
					auto result = nodes[i]->worker().try_submit(event);
					if (result == SubmitResult::Accepted) {
						messages[i].pop_front();
					}
				}
			}
			nodes[i]->service({clock, 1000 + i * 500});
			if (auto job = nodes[i]->worker().take_io_operation()) {
				kronuz::journal::drive_synchronously(*job);
			}
			for (auto &action : nodes[i]->take_network_actions()) {
				if (auto send = std::get_if<Send>(&action)) {
					auto peer = static_cast<unsigned>(send->peer - 1);
					if (connected[i][peer] && nodes[peer]) {
						assert(messages[peer].size() < 64);
						messages[peer].push_back({i + 1, std::move(send->message)});
					}
				}
				assert(!std::holds_alternative<Fenced>(action));
			}
			for (auto &result : nodes[i]->take_completions()) {
				completed[i].push_back(std::move(result));
			}
			assert(!nodes[i]->worker().fenced());
			if (auto output = nodes[i]->channel().outbound()) {
				auto peer = static_cast<unsigned>(output->session.peer - 1);
				assert(connected[i][peer] && nodes[peer] && sessions[peer][i]);
				auto bytes = output->bytes.first(std::min<std::size_t>(8192, output->bytes.size()));
				auto received = nodes[peer]->channel().receive(*sessions[peer][i], bytes);
				assert(received.result != TransferReceive::Closed);
				if (received.consumed) {
					nodes[i]->channel().consume_output(output->token, received.consumed);
				}
			}
		}
	}
	void until(const std::function<bool()> &predicate, unsigned budget = 20000) {
		for (unsigned i = 0; i < budget && !predicate(); ++i) {
			step();
		}
		assert(predicate());
	}
	Completion request(unsigned node, std::uint64_t id, const Command &command) {
		bool submitted = false;
		until([&] {
			if (!submitted) {
				submitted = nodes[node]->propose(id, {command}) == SubmitResult::Accepted;
			}
			return submitted;
		});
		until([&] {
			return std::any_of(completed[node].begin(), completed[node].end(),
							   [&](auto &result) { return result.request == id; });
		});
		auto found = std::find_if(completed[node].begin(), completed[node].end(),
								  [&](auto &result) { return result.request == id; });
		auto result = std::move(*found);
		completed[node].erase(found);
		assert(!result.rejection && result.results.size() == 1);
		return result;
	}
	Command transaction(std::uint64_t sequence, int value) {
		Command c;
		c.context = context;
		c.producer = "writer";
		c.generation = 1;
		c.sequence = sequence;
		c.name_space = "ns";
		c.mutations.push_back({"ns:key", detent::IntDef{value}, {}, {}});
		return c;
	}
};
} // namespace
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
	assert(group.request(0, 11, group.transaction(1, 1)).results[0].status == Status::Applied);
	group.until([&] { return group.nodes[1]->state().revision() == 1; });
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
	std::cout << "three-Worker authority snapshot, retry and failover passed\n";
}
