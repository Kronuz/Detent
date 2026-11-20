#pragma once
#include "paged_transfer.hh"
#include <optional>
#include <span>
#include <string>

// Dense distribution D6 and D9 (docs/design/dense-distribution.md): which encoding carries an
// update, and what a delta receiver does with the pages it is sent.
namespace detent::delivery {
enum class Encoding : std::uint8_t { Records = 1, Pages = 2 };

// A body's bytes as sent (D8): 64 KiB fragments, each zstd-3 with its 96-byte header.
std::uint64_t sent_bytes(std::span<const char> body);

// D6 rule 1: raw pages above this many times the raw records go as records, uncompressed.
inline constexpr std::uint64_t shortcut_ratio = 3;
struct EncodingChoice {
	Encoding encoding = Encoding::Records;
	// Rule 1 decided; the sizes as sent are then not computed and stay 0.
	bool shortcut = false;
	std::uint64_t pages_raw = 0, records_raw = 0, pages_sent = 0, records_sent = 0;
};
// D6 rules 1 and 2 for one update the receiver's base can take either way: `pages` is the delta
// transfer, `records` the records answer's body. Pages go only when smaller as sent.
EncodingChoice choose_encoding(std::span<const char> pages, std::span<const char> records);

// What the receiver of a delta transfer does next (D9).
enum class DeltaOutcome : std::uint8_t {
	Published = 1,	 // verified: write and publish the received segments
	OtherSource = 2, // refused: ask one other source for the same transfer
	Records = 3,	 // refused again: take the records answer and apply it
	FullCopy = 4,	 // the base is damaged: namespace D10.3's full copy, as pages
};
struct DeltaReceipt {
	DeltaOutcome outcome = DeltaOutcome::Records;
	std::optional<local::detail::ReceivedTransfer> received; // Published only
	std::optional<local::detail::TransferRefusal> refusal;	 // when DD1's receiver refused
	std::string reason;
};
// D4 over the receiver's base, then D9: a base that lacks a reused page, or a base page that
// fails first touch, is damaged and takes the full copy; any other refusal tries one other source
// (`refused_sources` counts the sources this transfer's name was already refused from), then
// records. Nothing is published unless the outcome is Published.
DeltaReceipt receive_delta(std::shared_ptr<distribution::StorageLedger>, std::span<const char> stream,
						   const local::detail::TransferBase &base, const local::paged::Digest &target,
						   const local::detail::TransferSegmentId &, local::paged::Limits,
						   unsigned refused_sources = 0);
} // namespace detent::delivery
