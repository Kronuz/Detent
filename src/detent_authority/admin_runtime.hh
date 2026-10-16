#pragma once
#include "service.hh"

namespace detent::authority::admin {
class Runtime {
  public:
	explicit Runtime(const ServiceOptions &options);
	void ready();
	void recover_committed();
	Result submit(const Command &command);
	void checkpoint();
	Node node;

  private:
	void pump();
	std::uint64_t clock_ = 0, request_ = 0;
	std::uint64_t election_delay_;
	bool elect_ = true;
};
} // namespace detent::authority::admin
