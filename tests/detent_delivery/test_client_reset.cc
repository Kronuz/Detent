// A session control response maps to the reset an agent acts on: a voter that knows no live leader
// (503 not_ready) is Unavailable, which moves the agent to its next source, not AdmissionFull,
// which retries the same one (relay-mirror-idle-wake R2-2).
#include "../../src/detent_delivery/session_control_internal.hh"
#include <iostream>

namespace dd = detent::delivery;

int main() {
	using Reason = dd::SessionResetReason;
	struct Case {
		unsigned status;
		const char *error;
		Reason reason;
	};
	const Case cases[]{
		{503, "not_ready", Reason::Unavailable},
		{503, "stopping", Reason::Unavailable},
		{503, "source_not_leader", Reason::NotLeader},
		{503, "source_behind", Reason::SourceBehind},
		{503, "admission_full", Reason::AdmissionFull},
		{429, "rate_limited", Reason::AdmissionFull},
	};
	int failed = 0;
	for (const auto &c : cases) {
		const auto got = dd::detail::session_reset_reason(c.status, c.error, {});
		if (got != c.reason) {
			std::cerr << "client reset: " << c.status << " " << c.error << " mapped to " << int(got) << ", not "
					  << int(c.reason) << "\n";
			++failed;
		}
	}
	if (failed)
		return 1;
	std::cout << "client reset: ok\n";
	return 0;
}
