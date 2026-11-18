#pragma once
#include "service.hh"
#include <completion/posix.h>
#include <stdexcept>
#include <variant>

// Recovers one voter's committed state with no network and checkpoints it.
class OfflineRuntime {
  public:
	explicit OfflineRuntime(const detent::authority::ServiceOptions &options)
		: node(std::make_unique<kronuz::journal::PosixIO>(options.directory), options.node),
		  election_delay_(options.node.timing.election_min) {}

	void recover_committed() {
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

	void checkpoint() {
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
		throw std::runtime_error("authority checkpoint timeout");
	}

	detent::authority::Node node;

  private:
	static void check(bool condition, const char *message) {
		if (!condition)
			throw std::runtime_error(message);
	}
	void pump() {
		node.service({0, election_delay_});
		if (auto job = node.worker().take_io_operation())
			kronuz::journal::drive_synchronously(*job);
		if (auto job = node.take_history_operation())
			kronuz::journal::drive_synchronously(*job);
		for (const auto &action : node.take_network_actions())
			check(!std::holds_alternative<kronuz::consensus::Send>(action), "offline runtime requires one voter");
		check(!node.worker().fenced(), "authority storage fenced");
	}
	std::uint64_t election_delay_;
};
