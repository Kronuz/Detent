#include "detent_distribution_http/reply_pump.hh"

namespace detent::distribution_http {
void OwnedReplyPump::callbacks(http::StreamOptions &options, std::weak_ptr<OwnedReplyPump> weak) {
	options.on_writable = [weak] {
		if (auto held = weak.lock())
			held->writable.store(true);
	};
	options.reply.on_settled = [weak](http::ReplySettlement outcome) {
		if (auto held = weak.lock()) {
			held->written.store(outcome == http::ReplySettlement::Written);
			held->settled.store(true);
		}
	};
}
void OwnedReplyPump::send_control(int status, std::string_view body,
								  std::shared_ptr<distribution::StorageLedger> account) noexcept {
	if (!reply || started || settled.load())
		return;
	try {
		http::Response metadata;
		metadata.status = status;
		metadata.content_type = "application/json";
		auto bytes = distribution::make_owned_buffer(account, body.size());
		bytes->append(body);
		bytes->seal();
		if (!bytes->empty()) {
			control = std::move(bytes);
			if (reply->start(metadata, control->size()) != http::StreamSubmit::Accepted)
				reply->abandon();
			return;
		}
		if (reply->start(metadata, 0) != http::StreamSubmit::Accepted ||
			reply->finish() != http::StreamSubmit::Accepted)
			reply->abandon();
	} catch (...) {
		reply->abandon();
	}
}
http::StreamSubmit OwnedReplyPump::publish(std::shared_ptr<const distribution::OwnedBuffer> &pending) {
	if (!pending || !writable.exchange(false))
		return http::StreamSubmit::Busy;
	const auto result = reply->publish({pending, pending->view()});
	if (result == http::StreamSubmit::Accepted)
		pending.reset();
	return result;
}
void OwnedReplyPump::pump_control() noexcept {
	if (!control || settled.load())
		return;
	try {
		const auto result = publish(control);
		if (result == http::StreamSubmit::Accepted) {
			if (reply->finish() != http::StreamSubmit::Accepted)
				reply->abandon();
		} else if (result != http::StreamSubmit::Busy)
			reply->abandon();
	} catch (...) {
		reply->abandon();
	}
}
http::Response update_headers(distribution::UpdateKind kind, const distribution::Cursor &target,
							  bool compressed, std::optional<SessionHeaders> session) {
	http::Response metadata;
	metadata.status = 200;
	metadata.content_type = "application/vnd.detent.distribution";
	if (session) {
		constexpr char digits[] = "0123456789abcdef";
		std::string identity(32, '0');
		for (std::size_t i = 0; i < session->identity.size(); ++i) {
			const auto byte = static_cast<unsigned char>(session->identity[i]);
			identity[2 * i] = digits[byte >> 4];
			identity[2 * i + 1] = digits[byte & 15];
		}
		metadata.headers.emplace_back("X-Detent-Session-Version", "1");
		metadata.headers.emplace_back("X-Detent-Session", std::move(identity));
		metadata.headers.emplace_back("X-Detent-Window", std::to_string(session->window));
	}
	metadata.headers.emplace_back("X-Detent-Coding", compressed ? "zstd" : "raw");
	metadata.headers.emplace_back("X-Detent-Profile", "1");
	metadata.headers.emplace_back("X-Detent-Kind", std::to_string(static_cast<unsigned>(kind)));
	metadata.headers.emplace_back("X-Detent-Revision", std::to_string(target.revision));
	return metadata;
}
} // namespace detent::distribution_http
