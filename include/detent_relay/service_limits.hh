#pragma once
#include "detent_distribution/session_table.hh"

namespace detent::relay {
struct ServiceLimits {
	std::size_t opens = 16, polls = 640, closes = 16;
	std::size_t bootstrap_transfers = 512, catchup_transfers = 128;
	distribution::SubscriptionLimits sessions{1024, 1024};
};
} // namespace detent::relay
