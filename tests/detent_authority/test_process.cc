#include "node.hh"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <journal/posix.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace detent::authority;
using namespace cluster::consensus;
namespace {
NodeOptions options() {
	NodeOptions o;
	o.context.cluster[0] = 'c';
	o.context.group[0] = 'g';
	o.context.epoch[0] = 'e';
	o.configuration.cluster = o.context.cluster;
	o.configuration.configuration[0] = 'v';
	o.configuration.local = 1;
	o.configuration.voters = {1};
	o.application.producers = 16;
	o.application.namespaces = 64;
	o.application.state_bytes = 2 * 1024 * 1024;
	o.application.image_bytes = 4 * 1024 * 1024;
	o.consensus.log_bytes = 4 * 1024 * 1024;
	return o;
}
Command transaction(std::uint64_t sequence) {
	Command c;
	c.context = options().context;
	c.producer = "writer";
	c.generation = 1;
	c.sequence = sequence;
	c.name_space = "ns";
	c.mutations.push_back({"ns:key", detent::IntDef{static_cast<std::int64_t>(sequence)}, {}, {}});
	return c;
}
std::uint64_t clock_value = 0;
void step(Node &node) {
	node.service({++clock_value, 100});
	node.take_network_actions();
	assert(!node.worker().fenced());
}
void start(Node &node) {
	for (unsigned i = 0; !node.worker().ready() && i < 1000; ++i) {
		step(node);
	}
	node.start();
	for (unsigned i = 0; node.worker().role() != Role::Leader && i < 2000; ++i) {
		step(node);
	}
	assert(node.worker().role() == Role::Leader);
}
Completion request(Node &node, std::uint64_t id, const Command &command) {
	bool submitted = false;
	for (unsigned i = 0; i < 2000; ++i) {
		if (!submitted) {
			submitted = node.propose(id, {command}) == SubmitResult::Accepted;
		}
		step(node);
		for (auto &result : node.take_completions()) {
			if (result.request == id) {
				return result;
			}
		}
	}
	throw std::runtime_error("process transaction did not complete");
}
void signal(int fd, char byte) {
	if (::write(fd, &byte, 1) != 1) {
		_exit(3);
	}
}
void expect(int fd, char expected) {
	pollfd descriptor{fd, POLLIN, 0};
	assert(::poll(&descriptor, 1, 30000) == 1);
	char byte = 0;
	assert(::read(fd, &byte, 1) == 1 && byte == expected);
}
void killed(pid_t child) {
	assert(::kill(child, SIGKILL) == 0);
	int status;
	assert(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}
} // namespace
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("authority-process-" + std::to_string(getpid()));
	std::filesystem::create_directories(directory);
	chmod(directory.c_str(), 0700);
	int channel[2];
	assert(::pipe(channel) == 0);
	auto child = ::fork();
	assert(child >= 0);
	if (!child) {
		::close(channel[0]);
		try {
			Node node(std::make_unique<kronuz::journal::PosixIO>(directory), options());
			Identity id{};
			id[0] = 's';
			node.create(id);
			start(node);
			Command open;
			open.context = options().context;
			open.kind = Kind::Open;
			open.producer = "writer";
			open.nonce[0] = 'n';
			assert(request(node, 1, open).results[0].generation == 1);
			auto result = request(node, 2, transaction(1));
			assert(result.results[0].revision == 1);
			signal(channel[1], 'A');
			for (unsigned i = 0; node.worker().applied_index() != node.state().applied_index() && i < 2000;
				 ++i) {
				step(node);
			}
			auto old = node.worker().storage_frontier().generation;
			assert(node.checkpoint());
			for (unsigned i = 0; node.worker().storage_frontier().generation == old && i < 2000; ++i) {
				step(node);
			}
			assert(node.worker().storage_frontier().generation != old && node.worker().base_index() == 0);
			signal(channel[1], 'P');
			for (;;) {
				::pause();
			}
		} catch (...) {
			signal(channel[1], 'F');
			_exit(1);
		}
	}
	::close(channel[1]);
	expect(channel[0], 'A');
	expect(channel[0], 'P');
	killed(child);
	::close(channel[0]);
	assert(::pipe(channel) == 0);
	child = ::fork();
	assert(child >= 0);
	if (!child) {
		::close(channel[0]);
		try {
			Node node(std::make_unique<kronuz::journal::PosixIO>(directory), options());
			node.recover();
			start(node);
			assert(node.state().lookup("writer", 1, 1).revision == 1);
			auto result = request(node, 3, transaction(2));
			assert(result.results[0].revision ==
				   2); // Deliberately lose this result before the client receives it.
			signal(channel[1], 'U');
			for (;;) {
				::pause();
			}
		} catch (...) {
			signal(channel[1], 'F');
			_exit(1);
		}
	}
	::close(channel[1]);
	expect(channel[0], 'U');
	killed(child);
	::close(channel[0]);
	{
		Node node(std::make_unique<kronuz::journal::PosixIO>(directory), options());
		node.recover();
		start(node);
		assert(node.state().lookup("writer", 1, 1).revision == 1 &&
			   node.state().lookup("writer", 1, 2).revision == 2 && node.state().revision() == 2);
		assert(request(node, 4, transaction(2)).results[0].revision == 2 && node.state().revision() == 2);
	}
	std::filesystem::remove_all(directory);
	std::cout << "acknowledged and uncertain results survive actual process kills\n";
}
