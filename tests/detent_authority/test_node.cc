#include "node.hh"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <journal/posix.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
using namespace cluster::consensus;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
NodeOptions options() {
	NodeOptions o;
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
} // namespace
int main() {
	auto path = std::filesystem::current_path() / ".scratch" / ("authority-node-" + std::to_string(getpid()));
	std::filesystem::create_directories(path);
	chmod(path.c_str(), 0700);
	std::uint64_t revision, checkpoint_index;
	{
		Node node(std::make_unique<kronuz::journal::PosixIO>(path), options());
		Identity id{};
		id[0] = 's';
		node.create(id);
		pump(node);
		node.start();
		pump(node);
		assert(node.worker().role() == Role::Leader);
		submit(node, 1, open());
		auto opened = pump(node);
		assert(opened.size() == 1 && opened[0].results[0].generation == 1);
		submit(node, 2, transaction(1, 10));
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
			node.take_network_actions();
		}
		assert(node.worker().ready());
		node.start();
		for (unsigned i = 0; i < 1000 && !node.worker().fenced(); ++i) {
			node.service({i, 100});
			node.take_network_actions();
		}
		assert(node.worker().fenced() && node.state().size() == 0);
	}
	std::filesystem::remove_all(plain);
	std::filesystem::remove_all(path);
	std::cout << "owned authority checkpoint/replay passed\n";
}
