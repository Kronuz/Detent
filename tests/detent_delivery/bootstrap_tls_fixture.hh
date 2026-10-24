#pragma once
#include "detent_delivery/client.hh"
#include "detent_relay/service.hh"
#include <http_asio.h>
#include <http_asio_tls.h>
#include <sha256.h>
#include <atomic>
#include <functional>
#include <source_location>

namespace bootstrap_tls_fixture {
namespace d = detent::distribution;
namespace r = detent::relay;
namespace t = detent::delivery;
inline void check(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("TLS bootstrap invariant at " + std::to_string(at.line()));
}
inline asio::awaitable<void> run(std::shared_ptr<r::Bundle> bundle,
    std::shared_ptr<d::StorageLedger> account, const std::filesystem::path &certificates,
    bool compressed, std::function<void()> write_next) {
    const auto executor = co_await asio::this_coro::executor;
    auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
    reactor::tls::configure(*server_tls,
        {(certificates / "one.pem").string(), (certificates / "one.key").string(),
         (certificates / "ca.pem").string()}, reactor::tls::Role::Server);
    auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
    reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
    auto target = *bundle->status().selected;
    const std::array<std::string_view, 1> names{"app"};
    r::ConfiguredCredential credential;
    credential.authorization.principal.identity.fill('p');
    credential.authorization.principal.policy_epoch = 1;
    credential.authorization.grants.fill('g');
    credential.authorization.bundles[0] = 1;
    const std::string token = "0123456789abcdef0123456789abcdef";
    class SHA256 hash; hash.add(token.data(), token.size()); hash.getHash(credential.token_hash.data());
    r::ServiceAccounts accounts{std::make_shared<d::StorageLedger>(account, 2 * 1024 * 1024),
        {std::make_shared<d::StorageLedger>(account, 4 * 1024 * 1024),
         std::make_shared<d::StorageLedger>(account, 4 * 1024 * 1024),
         std::make_shared<d::StorageLedger>(account, 2 * 1024 * 1024)}};
    auto registry = std::make_shared<r::Registry>(accounts.metadata,
        std::array{r::RegistryBundle{target, credential.authorization.principal, names}}, std::array{credential});
    const std::array bindings{r::SessionBinding{bundle, credential.authorization.principal, target}};
    auto runtime = std::make_shared<r::ServiceRuntime>(accounts, registry, bindings);
    r::Service service(runtime);
    asio::ip::tcp::acceptor probe(executor, {asio::ip::address_v4::loopback(), 0});
    const auto endpoint = probe.local_endpoint(); probe.close();
    http::HttpAsioService listener(service, 2, 2, 8, http::tls_connection_factory_timed(server_tls), 32);
    http::AsioBindOptions bind; bind.address = "127.0.0.1"; listener.set_bind_options(bind);
    listener.start(endpoint.port());
    t::ClientOptions options;
    options.endpoint = endpoint; options.hostname = "localhost"; options.token = token;
    options.tls_context = client_tls; options.configured = target; options.namespaces = {"app"};
    options.compression = compressed; options.timeout = std::chrono::seconds(5);
    options.limits.storage_account = account;
    bool complete = false;
    std::exception_ptr failure;
    asio::cancellation_signal cancel;
    auto client = [&]() -> asio::awaitable<void> {
        auto session = co_await t::open_session(options, true);
        check(session.valid());
        for (unsigned i = 0; i != 3; ++i) write_next();
        asio::steady_timer delay(executor); delay.expires_after(std::chrono::milliseconds(5));
        co_await delay.async_wait(asio::use_awaitable);
        std::optional<t::PublicationFeedback> feedback;
        for (auto revision : {42u, 45u, 48u}) {
            auto event = co_await t::next_session(options, session, feedback, std::chrono::milliseconds(0));
            auto *offer = std::get_if<t::SessionOffer>(&event);
            check(offer && offer->update.target.revision == revision);
            check(offer->update.kind == (revision == 42 ? d::UpdateKind::Snapshot : d::UpdateKind::Progress));
            if (revision == 42) check(offer->update.snapshot && offer->update.snapshot->records().size() == 2);
            else check(offer->update.delta && offer->update.delta->base().revision == revision - 3);
            feedback = t::PublicationFeedback{offer->window, offer->update.target};
            write_next();
        }
        auto event = co_await t::next_session(options, session, feedback, std::chrono::milliseconds(0));
        auto *reset = std::get_if<t::SessionReset>(&event);
        check(reset && reset->reason == t::SessionResetReason::HistoryLost);
        co_await t::close_session(options, session);
    };
    asio::co_spawn(executor, client(), asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr error) {
        failure = error; complete = true;
    }));
    asio::steady_timer tick(executor);
    const auto deadline = r::Sessions::Clock::now() + std::chrono::seconds(15);
    bool canceled = false;
    while (!complete) {
        runtime->step(r::Sessions::Clock::now());
        if (!canceled && r::Sessions::Clock::now() >= deadline) {
            canceled = true; cancel.emit(asio::cancellation_type::terminal);
        }
        tick.expires_after(std::chrono::milliseconds(1)); co_await tick.async_wait(asio::use_awaitable);
    }
    runtime->close();
    std::atomic<bool> stopped{false};
    asio::thread_pool worker(1);
    asio::post(worker, [&] { listener.stop(); stopped.store(true); });
    while (!stopped.load() || !runtime->drained()) {
        runtime->step(r::Sessions::Clock::now());
        tick.expires_after(std::chrono::milliseconds(1)); co_await tick.async_wait(asio::use_awaitable);
    }
    worker.join();
    if (failure) std::rethrow_exception(failure);
    check(!canceled);
}
} // namespace bootstrap_tls_fixture
