#pragma once
#include "builder.hh"
#include <completion/image.h>
namespace detent::local {
namespace completion = kronuz::io::completion;
enum class PublicationState { Idle, Recovering, Preparing, Published, Rejected, Cancelled, Closed, Fenced };
// Single-owner build/IO worker. An accepted operation retains the backend,
// owner lock and application admission lease through original settlement.
class ImagePublisher {
  public:
	ImagePublisher(std::shared_ptr<completion::IO> io, bool create_owner, std::shared_ptr<void> admission,
				   std::optional<RecoveredGeneration> recovered = {});
	~ImagePublisher();
	ImagePublisher(const ImagePublisher &) = delete;
	void begin(const ImageBuilder &builder, std::optional<distribution::Cursor> expected_base = {});
	std::shared_ptr<completion::Operation> operation() const;
	void advance();
	void cancel() noexcept;
	void close() noexcept;
	PublicationState state() const noexcept;
	bool uncertain() const noexcept;
	std::optional<distribution::Cursor> published_cursor() const;
	std::exception_ptr error() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace detent::local
