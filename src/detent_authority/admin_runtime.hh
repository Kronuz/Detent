#pragma once
#include "service.hh"

namespace detent::authority::admin {
class Runtime {
  public:
	explicit Runtime(const ServiceOptions &options);
	void recover_committed();
	void checkpoint();
	Node node;

  private:
	void pump();
	std::uint64_t election_delay_;
};
} // namespace detent::authority::admin
