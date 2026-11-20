#pragma once
#include "detent_distribution/buffer.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/retirement.hh"
#include <array>
#include <optional>

namespace detent::distribution_http {
enum class RequestKind { Distribution, Open, Next, Close };
std::optional<RequestKind> classify_session_route(std::string_view) noexcept;
struct RequestLimits {
	std::size_t bytes = 256 * 1024;
	std::uint32_t namespaces = 4096;
	std::uint64_t maximum_wait_ms = 30000;
};
// W5: a namespace the client follows and, when it holds a store of it, where that store is
// (under the request's cluster). No cursor: no store, or one of another cluster.
struct NamespacePosition {
	std::string_view name;
	std::optional<distribution::NamespaceCursor> cursor;
};
// Dense distribution D8, next only: the page transfer the client holds a prefix of, named by its
// namespace, root digest and `after`, and the prefix's length, a whole number of chunks. A source
// whose answer is that transfer sends it from `offset`.
struct TransferResume {
	std::string_view name;
	std::array<char, 32> root{};
	std::uint64_t after = 0, offset = 0;
};
struct SessionRequest {
	RequestKind kind;
	// 1: the scope body (removed by namespace-s6-cutover); 2: W5's per-namespace body.
	std::uint64_t version = 1;
	distribution::Cursor identity;
	distribution::Identity session{};
	std::optional<distribution::Cursor> base, published;
	std::span<const std::string_view> namespaces;
	std::uint64_t after = 0, wait_ms = 0;
	bool compressed = false, force_snapshot = false;
	// Version 2. `positions` is in strictly increasing name order; `all` asks for every granted
	// namespace too, with the client's directory cursor.
	std::span<const NamespacePosition> positions;
	std::optional<distribution::DirectoryCursor> all;
	// Dense distribution DD3, open only: `"pages":true` lets a full copy come as a page transfer;
	// `"records"` names, strictly increasing, are answered with records regardless.
	bool pages = false;
	std::span<const std::string_view> records;
	std::optional<TransferResume> resume;
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
	parse_session_request(std::shared_ptr<distribution::StorageLedger>, RequestKind,
									std::string_view, RequestLimits = {});
} // namespace detent::distribution_http
