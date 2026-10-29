#include "observer.hh"
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_distribution/delta_encoder.hh"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <source_location>

namespace d = detent::distribution;
using Clock = std::chrono::steady_clock;
void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("production codec fixture at " + std::to_string(location.line()));
}
std::size_t integer(std::string_view text) {
    std::size_t value{};
    auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), value);
    require(error == std::errc{} && end == text.data()+text.size());
    return value;
}
d::Cursor cursor() {
    d::Cursor value;
    value.cluster.fill('c'); value.group.fill('g'); value.restore_epoch.fill('e'); value.scope.fill('s');
    value.revision = 42;
    return value;
}
struct Totals {
    std::int64_t cpu = 0, wall = 0, maximum_step = 0;
    std::uint64_t allocations = 0, steps = 0;
    void add(const Result &result) { cpu += result.thread_ns; wall += result.wall_ns; allocations += result.allocations; }
    template<class F> void step(bool observed, F action) {
        if (observed) {
            const auto begin = Clock::now();
            action();
            maximum_step = std::max(maximum_step, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count());
        } else action();
        ++steps;
    }
    void print(std::string_view name, bool observed) const {
        std::cout << '"' << name << "\":{\"caller_cpu_ns\":" << cpu << ",\"wall_ns\":" << wall
                  << ",\"allocation_events\":";
        if (observed) std::cout << allocations; else std::cout << "null";
        std::cout << ",\"steps\":" << steps << ",\"observed_max_step_ns\":";
        if (observed) std::cout << maximum_step; else std::cout << "null";
        std::cout << '}';
    }
};
template<class Decoder, class Encoder, class Factory>
void run(std::string_view wire, std::size_t repeats, std::size_t fragment, bool observed, Factory factory) {
    Totals decode, encode, cleanup;
    std::size_t peak = 0, storage_peak = 0;
    auto parent = std::make_shared<d::StorageLedger>(d::Limits{}.storage_bytes);
    for (std::size_t round = 0; round != repeats; ++round) {
        auto account = std::make_shared<d::StorageLedger>(parent, parent->limit());
        d::Limits limits;
        limits.storage_account = account;
        using Value = decltype(std::declval<Decoder &>().finish());
        std::shared_ptr<Value> value;
        decode.add(phase("decode", [&] {
            Decoder decoder(cursor(), {"app"}, limits);
            for (std::size_t offset = 0; offset != wire.size();) {
                const auto size = std::min(fragment, wire.size()-offset);
                auto input = std::span(wire.data()+offset, size);
                while (!input.empty()) {
                    d::WorkBudget budget;
                    decode.step(observed, [&] { decoder.feed(input, budget); });
                }
                offset += size;
            }
            value = std::make_shared<Value>(decoder.finish());
        }));
        storage_peak = std::max(storage_peak, value->peak_storage_capacity());
        encode.add(phase("encode", [&] {
            Encoder encoder(factory(std::shared_ptr<const Value>(value), account), limits);
            std::size_t offset = 0;
            while (!encoder.done() || encoder.chunk_ready()) {
                if (encoder.chunk_ready()) {
                    const auto chunk = encoder.take_chunk();
                    require(chunk && chunk->size() <= wire.size()-offset);
                    require(chunk->view() == wire.substr(offset, chunk->size()));
                    offset += chunk->size();
                } else {
                    d::WorkBudget budget;
                    encode.step(observed, [&] { encoder.step(budget); });
                }
            }
            require(offset == wire.size() && encoder.encoded_length() == wire.size());
        }));
        peak = std::max(peak, account->peak());
        cleanup.add(phase("cleanup", [&] {
            for (;;) {
                d::WorkBudget budget;
                bool done = false;
                cleanup.step(observed, [&] { done = value->discard(budget); });
                if (done) break;
            }
            value.reset();
        }));
        require(account->used() == 0 && parent->used() == 0);
    }
    std::cout << "{\"repeats\":" << repeats << ",\"fragment_bytes\":" << fragment
              << ",\"observed\":" << (observed ? "true" : "false")
              << ",\"logical_bytes\":" << wire.size() << ",\"peak_admitted_bytes\":" << peak
              << ",\"peak_decoder_arena_bytes\":" << storage_peak << ',';
    decode.print("decode", observed); std::cout << ','; encode.print("encode", observed); std::cout << ','; cleanup.print("cleanup", observed);
    rusage usage{}; require(::getrusage(RUSAGE_SELF, &usage) == 0);
    std::cout << ",\"process_peak_rss_bytes\":" << usage.ru_maxrss << "}\n";
}
int main(int argc, char **argv) {
    try {
        require(argc == 6);
        std::ifstream file(argv[2], std::ios::binary);
        require(file.good());
        const std::string wire{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        require(!wire.empty() && !file.bad());
        const auto repeats = integer(argv[3]), fragment = integer(argv[4]);
        require(repeats && fragment);
        const bool observed = std::string_view(argv[5]) == "observed";
        require(observed || std::string_view(argv[5]) == "plain");
        std::optional<AllocationLog> logger;
        if (observed) logger.emplace();
        (void)thread_cpu();
        if (std::string_view(argv[1]) == "snapshot")
            run<d::SnapshotDecoder, d::SnapshotEncoder>(wire, repeats, fragment, observed,
                [](auto value, auto account) { return d::make_snapshot_source(std::move(value), std::move(account)); });
        else {
            require(std::string_view(argv[1]) == "delta");
            run<d::DeltaDecoder, d::DeltaEncoder>(wire, repeats, fragment, observed,
                [](auto value, auto account) { return d::make_delta_source(std::move(value), 1, std::move(account)); });
        }
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
