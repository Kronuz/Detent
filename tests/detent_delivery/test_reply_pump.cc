#include "detent_distribution/memory_resource.hh"
#include "detent_distribution_http/reply_pump.hh"
#include <iostream>
#include <source_location>

namespace d = detent::distribution;
namespace h = detent::distribution_http;
void check(bool value, std::source_location location = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("reply pump invariant at " + std::to_string(location.line()));
}
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(1024 * 1024);
		for (bool cancel : {false, true}) {
			{
				auto pump =
					std::allocate_shared<h::OwnedReplyPump>(d::StorageAllocator<h::OwnedReplyPump>(account));
				http::StreamOptions options;
				options.resource = d::make_storage_resource(account);
				options.reply.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				auto request = std::make_shared<int>();
				std::weak_ptr<int> retained_request = request;
				options.reply.request_retention = request;
				h::OwnedReplyPump::callbacks(options, pump);
				auto exchange = std::allocate_shared<http::detail::OwnedStreamExchange>(
					d::StorageAllocator<http::detail::OwnedStreamExchange>(account), std::move(options),
					[] {});
				pump->reply = http::detail::make_resource_unique<http::OwnedStreamReply>(
					d::make_storage_resource(account), exchange);
				request.reset();
				pump->send_control(503, "{\"error\":\"source_behind\"}", account);
				std::weak_ptr<const d::OwnedBuffer> retained_body = pump->control;
				auto header = exchange->take_write();
				check(header && header->head.find("503") != std::string_view::npos);
				pump->pump_control();
				check(pump->control && !pump->writable.load());
				check(exchange->complete_write(header->serial, http::ReplySettlement::Written));
				header.reset();
				check(pump->writable.load());
				pump->pump_control();
				check(!pump->control && !pump->settled.load() && !pump->written.load());
				auto body = exchange->take_write();
				check(body && body->chunk.bytes == "{\"error\":\"source_behind\"}");
				if (cancel)
					pump->reply->abandon();
				check(!exchange->settle() && !retained_body.expired() && !retained_request.expired());
				check(!exchange->complete_write(body->serial + 1, http::ReplySettlement::Written));
				check(exchange->complete_write(body->serial, http::ReplySettlement::Written));
				check(!exchange->complete_write(body->serial, http::ReplySettlement::Written));
				body.reset();
				check(retained_body.expired() && !pump->settled.load());
				check(exchange->settle() && pump->settled.load() && pump->written.load() == !cancel);
				check(retained_request.expired() && !exchange->settle());
			}
			check(account->used() == 0);
		}
		for (bool compressed : {false, true}) {
			d::Cursor target;
			target.revision = 42;
			d::Identity session;
			session.fill('\xff');
			auto metadata =
				h::update_headers(d::UpdateKind::Delta, target, compressed, h::SessionHeaders{session, 7});
			check(metadata.status == 200 && metadata.content_type == "application/vnd.detent.distribution");
			check(metadata.headers.size() == 7);
			check(metadata.headers[1].second == std::string(32, 'f'));
			check(metadata.headers[2].second == "7");
			check(metadata.headers[3].second == (compressed ? "zstd" : "raw"));
			check(metadata.headers[5].second == "2" && metadata.headers[6].second == "42");
		}
		std::cout << "Authentic settlement, retained originals and common headers passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
