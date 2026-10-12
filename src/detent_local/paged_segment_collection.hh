#pragma once
#include "detent_local/storage_session.hh"
namespace detent::local::detail {
struct StoreCore;
class PagedSegmentCollection final : public kronuz::io::completion::Operation {
  public:
	PagedSegmentCollection(distribution::RetirementFactory, std::shared_ptr<StoreCore>);
	~PagedSegmentCollection();
	const kronuz::io::completion::Request &request() const override;
	void submitted() override;
	bool complete(kronuz::io::completion::Result) noexcept override;
	bool done() const noexcept override;
	bool in_flight() const noexcept override;
	kronuz::io::completion::IO &io() const noexcept override;
	void start();
	void advance();
	bool needs_work() const noexcept;
	void step(ValidationBudget &);
	bool paused() const noexcept;
	void resume();
	void invalidate() noexcept;
	void close() noexcept;
	std::exception_ptr error() const noexcept;

  private:
	struct State;
	distribution::RetirementHandle<State> state_;
	std::shared_ptr<distribution::RetirementHandle<State>> pin_;
	kronuz::io::completion::Request request_;
	void prepare(kronuz::io::completion::Kind);
	void next();
	void cleanup();
	void fail(std::exception_ptr) noexcept;
};
} // namespace detent::local::detail
