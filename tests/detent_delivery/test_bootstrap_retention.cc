#include "detent_relay/sessions.hh"
#include "detent_distribution/framing.hh"
#include "paged_disk_fixture.hh"
#include <source_location>
#ifdef DETENT_TEST_BOOTSTRAP_TLS
#include "bootstrap_tls_fixture.hh"
#endif
namespace r = detent::relay;
void check(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("bootstrap retention invariant at " + std::to_string(at.line()));
}
d::Cursor at(std::uint64_t revision) { auto c = cursor(); c.revision = revision; return c; }
std::string progress(std::uint64_t base, std::uint64_t target) {
    std::string out;
    m::array(out, 5); m::unsigned_integer(out, 1); m::unsigned_integer(out, 3);
    m::array(out, 2); m::unsigned_integer(out, 1); m::unsigned_integer(out, 1);
    for (auto c : {at(base), at(target)}) {
        m::array(out, 5);
        for (auto *field : {&c.cluster, &c.group, &c.restore_epoch})
            m::string(out, {field->data(), field->size()}, true);
        m::string(out, {c.scope.data(), c.scope.size()}, true);
        m::unsigned_integer(out, c.revision);
    }
    return out;
}
void run(bool compressed, const std::filesystem::path &certificates = {}) {
    const auto directory = std::filesystem::current_path() / ".scratch" /
        ("bootstrap-retention-" + std::to_string(::getpid()) + (compressed ? "-zstd" : "-raw"));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    auto heap = std::make_shared<d::StorageLedger>(32 * 1024 * 1024);
    auto mappings = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
    auto controls = std::make_shared<d::StorageLedger>(1024 * 1024);
    d::RetirementRuntime runtime(controls, 1);
    auto domain = runtime.domain(heap, 512);
    const auto resident_heap = heap->used();
    {
        l::ViewLimits limits; limits.retirement_domain = &domain; limits.mapping_account = mappings;
        std::filesystem::create_directories(directory);
        check(::chmod(directory.c_str(), 0700) == 0);
        r::BundleOptions options; options.configured = cursor(); options.incarnation.fill('i');
        options.namespaces = 1; options.retained_edges = 2;
        auto bundle = std::make_shared<r::Bundle>(options, domain);
        bundle->authenticated();
        auto select = [&](std::uint64_t revision, bool staged) {
            auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture(1, 24, 25, revision), at(revision)));
            l::GenerationId id{}; id.fill(static_cast<char>(revision));
            l::ImageBuilder builder(source, id);
            while (!builder.done()) { l::ValidationBudget budget; builder.step(budget); }
            const auto name = l::generation_name(id);
            write(directory / (name + ".image"), *builder.bytes());
            write(directory / (name + ".lease"), {});
            write(directory / "selected", l::encode_descriptor(builder.descriptor()));
            auto generation = l::recover_generation(directory, limits);
            if (staged) bundle->bind_candidate(builder.descriptor(), generation.directory_identity());
            bundle->selected(generation);
        };
        select(42, false);
        auto advance = [&](std::uint64_t from, std::uint64_t target) {
            const auto wire = progress(from, target);
            d::DeltaDecoder decoder(at(from), {"app"}, {}, d::UpdateKind::Progress);
            auto input = std::span(wire.data(), wire.size());
            while (!input.empty()) { d::WorkBudget budget; decoder.feed(input, budget); }
            auto delta = std::make_shared<d::ValidatedDelta>(decoder.finish());
            detent::delivery::ReceivedUpdate received{d::UpdateKind::Progress, at(target), {}, delta};
            bundle->begin_stage(received);
            if (from == 42) {
                unsigned turns = 0;
                while (true) {
                    d::WorkBudget budget;
                    if (bundle->snapshot_step(budget)) break;
                    check(++turns < 10000);
                }
            }
            unsigned steps = 0;
            while (true) { d::WorkBudget budget; if (bundle->stage_step(budget)) break; check(++steps < 10000); }
            select(target, true);
        };
        advance(42, 45); advance(45, 48);
#ifdef DETENT_TEST_BOOTSTRAP_TLS
        if (!certificates.empty()) {
            std::uint64_t live = 48;
            asio::io_context io;
            auto write_next = [&] { advance(live, live + 3); live += 3; };
            auto finished = asio::co_spawn(io, bootstrap_tls_fixture::run(bundle, heap, certificates,
                compressed, write_next), asio::use_future);
            io.run(); finished.get();
        } else
#endif
        {
        r::SessionAuthorization auth; auth.principal.identity.fill('p'); auth.principal.policy_epoch = 1;
        auth.grants.fill('g'); auth.bundles[0] = 1;
        const std::array bindings{r::SessionBinding{bundle, auth.principal, cursor()}};
        r::Sessions sessions(heap, bindings);
        d::Identity id{}; id.fill('s');
        const auto now = r::Sessions::Clock::now();
        const auto before_pressure = heap->used();
        {
            auto pressure = heap->admit(heap->limit() - heap->used());
            check(sessions.open(id, auth, 0, {}, true, compressed, now) == d::SubscriptionResult::Busy);
            check(sessions.size() == 0 && !sessions.metadata(id, now));
        }
        check(heap->used() <= before_pressure);
        {
            auto metadata = std::make_shared<d::StorageLedger>(1024 * 1024);
            r::Sessions unfunded(metadata, bindings);
            auto pressure = metadata->admit(metadata->limit() - metadata->used());
            check(unfunded.open(id, auth, 0, {}, true, compressed, now) == d::SubscriptionResult::Busy);
            check(unfunded.size() == 0);
        }
        check(sessions.open(id, auth, 0, at(45), true, compressed, now) == d::SubscriptionResult::Busy);
        check(sessions.size() == 0);
        {
            d::SubscriptionLimits expiry_limits;
            expiry_limits.sessions = expiry_limits.per_principal = 1;
            expiry_limits.idle_lifetime = std::chrono::milliseconds(1);
            expiry_limits.maximum_lifetime = std::chrono::milliseconds(2);
            r::Sessions expiring(heap, bindings, expiry_limits);
            check(expiring.open(id, auth, 0, {}, true, compressed, now) == d::SubscriptionResult::Accepted);
            auto request = expiring.next(id, auth, 0, {}, now);
            check(request.token.has_value());
            auto offer = expiring.prepare(*request.token, now);
            check(offer.token && offer.update);
            auto original = offer.update->frame(0, compressed);
            const auto later = now + std::chrono::milliseconds(3);
            d::WorkBudget cleanup_work;
            expiring.collect(cleanup_work, later);
            check(expiring.size() == 1 && !original->view().empty());
            expiring.settle(*offer.token, true);
            cleanup_work = {};
            expiring.collect(cleanup_work, later);
            check(expiring.drained() && !original->view().empty());
        }
        check(sessions.open(id, auth, 0, at(42), true, compressed, now) == d::SubscriptionResult::Accepted);
        advance(48, 51); advance(51, 54); advance(54, 57);
        std::uint64_t window = 0;
        std::optional<d::Cursor> published;
        for (auto revision : {42, 45, 48}) {
            auto request = sessions.next(id, auth, window, published, now);
            check(request.token.has_value());
            auto offered = sessions.prepare(*request.token, now);
            check(offered.token && offered.update && offered.update->target() == at(revision));
            check(offered.update->kind() == (revision == 42 ? d::UpdateKind::Snapshot : d::UpdateKind::Progress));
            d::FragmentDecoder frames(offered.update->kind(), offered.update->logical_bytes(), compressed, heap);
            std::string decoded;
            for (std::size_t i = 0; i != offered.update->fragments(); ++i) {
                auto frame = offered.update->frame(i, compressed);
                auto bytes = std::span(frame->view().data(), frame->size());
                while (!bytes.empty()) {
                    d::WorkBudget budget; frames.feed(bytes, budget);
                    if (auto fragment = frames.take()) decoded.append(fragment->bytes());
                }
            }
            frames.finish();
            check(decoded == (revision == 42 ? fixture() : progress(revision - 3, revision)));
            auto early = sessions.next(id, auth, offered.token->window, at(revision), now);
            check(early.token.has_value());
            check(sessions.prepare(*early.token, now).result == d::SubscriptionResult::AwaitSettlement);
            sessions.release(*early.token);
            sessions.settle(*offered.token, true);
            check(sessions.next(id, auth, offered.token->window, at(revision + 1), now).result ==
                  d::SubscriptionResult::OutOfSync);
            window = offered.token->window; published = at(revision);
        }
        auto request = sessions.next(id, auth, window, published, now);
        check(request.token.has_value());
        check(sessions.prepare(*request.token, now).result == d::SubscriptionResult::SnapshotRequired);
        sessions.release(*request.token);
        sessions.close_all();
        while (!sessions.drained()) { d::WorkBudget budget; sessions.collect(budget, now); }
        check(bundle->status().snapshots == 1);
        }
        bundle->fence();
    }
    domain.close().wait();
    check(mappings->used() == 0 && heap->used() <= resident_heap);
}
int main(int argc, char **argv) {
    run(false); run(true);
    if (argc == 2) { run(false, argv[1]); run(true, argv[1]); }
}
