#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/retirement.hh"
#include <optional>

namespace detent::distribution_http {
enum class RequestKind { Distribution, Open, Next, Close };
std::optional<RequestKind> classify_session_route(std::string_view) noexcept;
struct RequestLimits {
	std::size_t bytes = 256 * 1024;
	std::uint32_t namespaces = 4096;
	std::uint64_t maximum_wait_ms = 30000;
};
struct SessionRequest {
	RequestKind kind;
	distribution::Cursor identity;
	distribution::Identity session{};
	std::optional<distribution::Cursor> base, published;
	std::span<const std::string_view> namespaces;
	std::uint64_t after = 0, wait_ms = 0;
	bool compressed = false, force_snapshot = false;
};
// The admitted parser arena owns every returned view until worker retirement.
class ParsedSessionRequest final : public distribution::RetirementNode {
  public:
	ParsedSessionRequest(std::shared_ptr<distribution::StorageLedger>, RequestKind,
						 std::shared_ptr<const distribution::OwnedBuffer>, RequestLimits);
	~ParsedSessionRequest();
	const SessionRequest &request() const noexcept;
	std::size_t storage_capacity() const noexcept;
	void parse();
	void parse(std::string_view);

  protected:
	bool retire_step() noexcept override;

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
distribution::RetirementHandle<ParsedSessionRequest>
parse_session_request(distribution::RetirementDomain &, RequestKind,
					  std::shared_ptr<const distribution::OwnedBuffer>, RequestLimits = {});
// Worker-only ownership: parsing and arena destruction stay off the owner loop.
distribution::StorageOwner<ParsedSessionRequest>
	parse_session_request_on_worker(std::shared_ptr<distribution::StorageLedger>, RequestKind,
									std::string_view, RequestLimits = {});
} // namespace detent::distribution_http
