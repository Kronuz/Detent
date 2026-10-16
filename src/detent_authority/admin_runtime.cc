#include "admin_runtime.hh"
#include <completion/posix.h>
#include <stdexcept>

namespace detent::authority::admin {
using namespace kronuz::consensus;
namespace {
void check(bool condition, const char *message) {
	if (!condition)
		throw std::runtime_error(message);
}
} // namespace
Runtime::Runtime(const ServiceOptions &options)
	: node(std::make_unique<kronuz::journal::PosixIO>(options.directory), options.node),
	  election_delay_(options.node.timing.election_min) {}
void Runtime::pump() {
	if (elect_)
		clock_ += 10;
	node.service({clock_, election_delay_});
	if (auto job = node.worker().take_io_operation())
		kronuz::journal::drive_synchronously(*job);
	for (const auto &action : node.take_network_actions())
		check(!std::holds_alternative<Send>(action), "offline migration requires one voter");
	check(!node.worker().fenced(), "authority storage fenced");
}
void Runtime::ready() {
	for (unsigned i = 0; i < 10000 && !node.worker().ready(); ++i)
		pump();
	check(node.worker().ready(), "authority initialization timeout");
	node.start();
	for (unsigned i = 0; i < 10000 && node.worker().role() != Role::Leader; ++i)
		pump();
	check(node.worker().role() == Role::Leader, "offline leader timeout");
}
void Runtime::recover_committed() {
	elect_ = false;
	node.recover();
	for (unsigned i = 0; i < 10000 && !node.worker().ready(); ++i)
		pump();
	check(node.worker().ready(), "authority recovery initialization timeout");
	node.start();
	for (unsigned i = 0; i < 100000; ++i) {
		pump();
		if (node.worker().applied_index() == node.worker().committed() &&
			node.state().applied_index() == node.worker().committed())
			return;
	}
	throw std::runtime_error("authority committed replay timeout");
}
Result Runtime::submit(const Command &command) {
	check(elect_, "offline recovery does not accept commands");
	auto id = ++request_;
	bool accepted = false;
	for (unsigned i = 0; i < 100000; ++i) {
		if (!accepted) {
			auto result = node.propose(id, {command});
			check(result == SubmitResult::Accepted || result == SubmitResult::Busy ||
					  result == SubmitResult::Pressure,
				  "migration submission rejected");
			accepted = result == SubmitResult::Accepted;
		}
		pump();
		for (auto &result : node.take_completions())
			if (result.request == RequestId{id}) {
				check(!result.rejection && result.results.size() == 1, "migration command rejected");
				return result.results.front();
			}
	}
	throw std::runtime_error("migration outcome unknown");
}
void Runtime::checkpoint() {
	bool started = false;
	std::uint64_t boundary = 0;
	for (unsigned i = 0; i < 100000; ++i) {
		pump();
		if (!started && node.worker().applied_index() == node.state().applied_index()) {
			boundary = node.state().applied_index();
			if (node.worker().base_index() == boundary)
				return;
			started = node.checkpoint();
		}
		if (started && node.worker().base_index() == boundary && !node.checkpoint_busy())
			return;
	}
	throw std::runtime_error("migration checkpoint timeout");
}
} // namespace detent::authority::admin
