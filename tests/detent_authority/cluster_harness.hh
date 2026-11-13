#pragma once
// An in-process group of three voters with partitions, restarts and snapshot installs.
#include "node.hh"
#include <completion/posix.h>
#include <journal/memory_io.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <source_location>
#include <tuple>
#include <sys/stat.h>
#include <unistd.h>

namespace detent::authority::testing {
using namespace kronuz::consensus;
inline bool completion_storage = false;
// When set, each voter keeps D6 history under this policy in `history_io(i)`,
// fetching what it lacks from its peers' directories, and gets the ring `ring(i)`.
inline std::optional<LayoutPolicy> history_policy;
inline std::function<std::shared_ptr<AppliedChangeRecorder>(unsigned)> ring;
// Adjusts voter i's options before it launches.
inline std::function<void(unsigned, NodeOptions &)> configure;
// Journals and history directories live in memory (journal's MemoryIO, kept across
// restarts), so a run waits on no device or filesystem; power cuts are the crash model's.
inline bool memory_storage = false;
struct Cluster {
	std::filesystem::path root;
	std::array<kronuz::journal::testing::Disk, 3> disks, history_disks;
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
		if (history_policy)
			o.application.layout = *history_policy;
		if (configure)
			configure(i, o);
		return o;
	}
	void launch(unsigned i, bool recover) {
		auto directory = root / std::to_string(i);
		std::filesystem::create_directories(directory);
		chmod(directory.c_str(), 0700);
		if (memory_storage)
			nodes[i] = std::make_unique<Node>(std::make_unique<kronuz::journal::testing::MemoryIO>(disks[i]), options(i));
		else
			nodes[i] = std::make_unique<Node>(std::make_unique<kronuz::journal::PosixIO>(directory), options(i));
		if (history_policy) {
			nodes[i]->attach_history(history_io(i),
									 [this, i](const std::string &name, history::Fetched done) {
										 if (deferred)
											 fetches.emplace_back(i, name, std::move(done));
										 else
											 done(fetch(i, name));
									 });
		}
		if (ring)
			nodes[i]->attach_change_recorder(ring(i));
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
			if (auto job = nodes[i]->take_history_operation())
				kronuz::journal::drive_synchronously(*job);
			nodes[i]->take_network_actions();
		}
		assert(nodes[i]->worker().ready());
		nodes[i]->start();
	}
	std::filesystem::path history_directory(unsigned i) const { return root / (std::to_string(i) + ".history"); }
	std::shared_ptr<kronuz::journal::IO> history_io(unsigned i) {
		if (memory_storage)
			return std::make_shared<kronuz::journal::testing::MemoryIO>(history_disks[i]);
		auto files = history_directory(i);
		std::filesystem::create_directories(files);
		chmod(files.c_str(), 0700);
		return std::make_shared<kronuz::journal::PosixIO>(files);
	}
	// Peer fetches answer at once, or wait in `fetches` while `deferred` until answered.
	bool deferred = false;
	std::vector<std::tuple<unsigned, std::string, history::Fetched>> fetches;
	void answer_fetches() {
		auto pending = std::move(fetches);
		fetches.clear();
		for (auto &[i, name, done] : pending)
			done(fetch(i, name));
	}
	// A voter whose history writes wait in `held` until released.
	std::array<bool, 3> held_history{};
	std::array<std::shared_ptr<kronuz::journal::IOOperation>, 3> held;
	void release_history(unsigned i) {
		if (held[i])
			kronuz::journal::drive_synchronously(*held[i]);
		held[i].reset();
	}
	// A peer serves a history file by name (D6 "Recovery"); every voter holds the same bytes.
	std::optional<std::string> fetch(unsigned i, std::string_view name) {
		for (unsigned j = 0; j < 3; ++j)
			if (j != i)
				if (auto bytes = history::Directory(history_io(j), {}).find(name))
					return bytes;
		return std::nullopt;
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
			if (auto job = nodes[i]->take_history_operation(); job && !held_history[i])
				kronuz::journal::drive_synchronously(*job);
			else if (job)
				held[i] = std::move(job);
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
	void until(const std::function<bool()> &predicate, unsigned budget = 20000,
			   std::source_location where = std::source_location::current()) {
		for (unsigned i = 0; i < budget && !predicate(); ++i) {
			step();
		}
		if (!predicate()) {
			std::cerr << "until timed out at " << where.file_name() << ":" << where.line() << "\n";
			std::abort();
		}
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
	Completion administer(unsigned node, std::uint64_t id, const AdministrativeCommand &command) {
		bool submitted = false;
		until([&] {
			if (!submitted)
				submitted = nodes[node]->propose_administrative(id, command) == SubmitResult::Accepted;
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
		c.mutations.push_back({"ns:key", detent::KnobDef{value}, {}, {}});
		return c;
	}
};
} // namespace detent::authority::testing
