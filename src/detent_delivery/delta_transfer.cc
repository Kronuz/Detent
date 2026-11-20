#include "delta_transfer.hh"
#include "detent_distribution/framing.hh"
#include "paged_page_access.hh"
#include <algorithm>

namespace detent::delivery {
namespace d = distribution;
namespace x = local::detail;

std::uint64_t sent_bytes(std::span<const char> body) {
	std::uint64_t sent = 0;
	for (std::size_t at = 0; at < body.size(); at += d::fragment_bytes) {
		const auto size = std::min(d::fragment_bytes, body.size() - at);
		sent += d::encode_fragment({d::UpdateKind::Frames, {}, at, body.size(), at + size == body.size(),
									std::string(body.data() + at, size)},
								   true)
					.size();
	}
	return sent;
}

EncodingChoice choose_encoding(std::span<const char> pages, std::span<const char> records) {
	EncodingChoice choice{Encoding::Records, false, pages.size(), records.size()};
	if (choice.pages_raw > shortcut_ratio * choice.records_raw) {
		choice.shortcut = true;
		return choice;
	}
	choice.pages_sent = sent_bytes(pages);
	choice.records_sent = sent_bytes(records);
	if (choice.pages_sent < choice.records_sent)
		choice.encoding = Encoding::Pages;
	return choice;
}

DeltaReceipt receive_delta(std::shared_ptr<d::StorageLedger> account, std::span<const char> stream,
						   const x::TransferBase &base, const local::paged::Digest &target,
						   const x::TransferSegmentId &segment_id, local::paged::Limits limits,
						   unsigned refused_sources) {
	try {
		return {DeltaOutcome::Published,
				x::receive_transfer(std::move(account), stream, &base, target, segment_id, limits),
				std::nullopt,
				{}};
	} catch (const x::TransferRefused &refused) {
		const auto outcome = refused.reason() == x::TransferRefusal::BaseLacks ? DeltaOutcome::FullCopy
							 : refused_sources == 0							 ? DeltaOutcome::OtherSource
																			 : DeltaOutcome::Records;
		return {outcome, std::nullopt, refused.reason(), refused.what()};
	} catch (const x::ReadSourceFailure &failure) {
		return {DeltaOutcome::FullCopy, std::nullopt, std::nullopt, failure.what()};
	}
}
} // namespace detent::delivery
