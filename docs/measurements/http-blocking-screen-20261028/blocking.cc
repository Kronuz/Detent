#include "detent/json_schema.hh"
#include "detent_http/http_client.hh"
#include <compressor_deflate.h>
#include <compressor_zstd.h>
#include <sha256.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <source_location>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void require(bool value, std::source_location source = std::source_location::current()) {
    if (!value) throw std::runtime_error("HTTP cost fixture failed at line " + std::to_string(source.line()));
}
std::size_t integer(const char *value) {
    std::size_t result = 0;
    const auto text = std::string_view(value);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    require(error == std::errc{} && end == text.data() + text.size());
    return result;
}
std::int64_t caller_cpu() {
    timespec time{};
    require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) == 0);
    return time.tv_sec * 1000000000LL + time.tv_nsec;
}
std::int64_t process_cpu(const rusage &usage) {
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000000LL +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1000;
}
std::string payload(std::size_t count) {
    detent::Snapshot snapshot;
    snapshot.version = 42;
    for (std::size_t index = 0; index != count; ++index) {
        std::array<char, 80> name{};
        require(std::snprintf(name.data(), name.size(), "bench:production.feature_%012zu", index) > 0);
        if (index % 2) {
            snapshot.knobs.emplace(name.data(), detent::IntDef{static_cast<std::int64_t>(index) * 7919});
        } else {
            detent::BoolDef definition;
            definition.default_value = index % 4 == 0;
            definition.rollout_pct = 50;
            definition.overrides.emplace("canary", detent::GroupRollout{25});
            definition.overrides.emplace("stable", false);
            snapshot.knobs.emplace(name.data(), std::move(definition));
        }
    }
    auto result = detent::serialize_snapshot(snapshot);
    auto parsed = detent::parse_source_snapshot(result, "benchmark", count, result.size());
    if (!parsed) throw std::runtime_error(parsed.error().detail);
    require(parsed.value().knobs.size() == count);
    return result;
}
std::string encoded(const std::string &body, std::string_view coding) {
    if (coding == "identity") return body;
    if (coding == "zstd") return compress_zstd(body);
    require(coding == "gzip");
    DeflateCompressData compressor(body.data(), body.size(), true);
    std::string result;
    for (const auto &block : compressor) result += block;
    return result;
}
class Server {
    std::string wire_;
    int listener_ = -1, peer_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_ = false;
    std::mutex mutex_;
    std::jthread worker_;
    std::exception_ptr failure_;
    std::uint64_t requests_ = 0, bytes_ = 0;
    struct Peer {
        Server &server;
        int fd;
        ~Peer() {
            std::lock_guard lock(server.mutex_);
            server.peer_ = -1;
            ::close(fd);
        }
    };
    void serve() noexcept {
        try {
            while (!stopping_) {
                pollfd event{listener_, POLLIN, 0};
                const auto ready = ::poll(&event, 1, 20);
                if (ready < 0 && errno == EINTR) continue;
                require(ready >= 0);
                if (!ready || stopping_) continue;
                const auto fd = ::accept(listener_, nullptr, nullptr);
                if (fd < 0 && stopping_) break;
                require(fd >= 0);
                Peer peer{*this, fd};
                {
                    std::lock_guard lock(mutex_);
                    peer_ = fd;
                    if (stopping_) ::shutdown(fd, SHUT_RDWR);
                }
                timeval timeout{3, 0};
                require(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
                require(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
#ifdef SO_NOSIGPIPE
                int one = 1;
                require(::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0);
#endif
                while (!stopping_) {
                    std::string request;
                    std::array<char, 4096> input;
                    while (request.find("\r\n\r\n") == std::string::npos) {
                        const auto bytes = ::recv(fd, input.data(), input.size(), 0);
                        if (bytes < 0 && errno == EINTR) continue;
                        if (bytes <= 0) break;
                        request.append(input.data(), static_cast<std::size_t>(bytes));
                        require(request.size() <= 65536);
                    }
                    if (request.empty() || stopping_) break;
                    require(request.ends_with("\r\n\r\n"));
                    for (std::size_t offset = 0; offset < wire_.size();) {
                        int flags = 0;
#ifdef MSG_NOSIGNAL
                        flags = MSG_NOSIGNAL;
#endif
                        const auto sent = ::send(fd, wire_.data() + offset, wire_.size() - offset, flags);
                        if (sent < 0 && errno == EINTR) continue;
                        require(sent > 0);
                        offset += static_cast<std::size_t>(sent);
                        bytes_ += static_cast<std::size_t>(sent);
                    }
                    ++requests_;
                }
            }
        } catch (...) { failure_ = std::current_exception(); }
    }
    void stop() noexcept {
        stopping_ = true;
        {
            std::lock_guard lock(mutex_);
            if (peer_ != -1) ::shutdown(peer_, SHUT_RDWR);
        }
        if (worker_.joinable()) worker_.join();
    }
  public:
    Server(std::string wire) : wire_(std::move(wire)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        require(listener_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        try {
            require(::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
            require(::listen(listener_, 4) == 0);
            socklen_t size = sizeof(address);
            require(::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
            port_ = ntohs(address.sin_port);
            worker_ = std::jthread([this] { serve(); });
        } catch (...) {
            ::close(listener_); listener_ = -1;
            throw;
        }
    }
    ~Server() { stop(); if (listener_ != -1) ::close(listener_); }
    std::uint16_t port() const noexcept { return port_; }
    void finish(std::size_t expected, std::size_t wire_size) {
        stop();
        if (failure_) std::rethrow_exception(failure_);
        require(requests_ == expected && bytes_ == expected * wire_size);
    }
};
}
int main(int argc, char **argv) {
    try {
        require(argc == 4);
        const auto count = integer(argv[1]), requests = integer(argv[2]);
        require(requests > 0);
        const std::string coding(argv[3]);
        const auto body = payload(count), compressed = encoded(body, coding);
        SHA256 hash;
        hash.add(body.data(), body.size());
        const auto digest = hash.getHash();
        std::string wire = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
            std::to_string(compressed.size()) + "\r\nETag: \"cost-v1\"\r\nConnection: keep-alive\r\n";
        if (coding != "identity") wire += "Content-Encoding: " + coding + "\r\n";
        wire += "\r\n" + compressed;
        const auto wire_bytes = wire.size();
        Server server(std::move(wire));
        std::vector<std::int64_t> latencies;
        latencies.reserve(requests);
        rusage before{}, after{};
        require(::getrusage(RUSAGE_SELF, &before) == 0);
        const auto wall_begin = Clock::now();
        const auto cpu_begin = caller_cpu();
        {
            detent_http::HttpConnection connection("127.0.0.1", server.port(), std::chrono::seconds(3));
            for (std::size_t i = 0; i != requests; ++i) {
                const auto start = Clock::now();
                const auto response = connection.get("/v1/snapshot");
                const auto stop = Clock::now();
                require(response.status == 200 && response.body == body &&
                    response.etag == std::optional<std::string>("\"cost-v1\"") && !response.is_patch);
                latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
            }
        }
        const auto cpu_end = caller_cpu();
        const auto wall_end = Clock::now();
        require(::getrusage(RUSAGE_SELF, &after) == 0);
        server.finish(requests, wire_bytes);
        std::sort(latencies.begin(), latencies.end());
        const auto rss_scale =
#ifdef __APPLE__
            1;
#else
            1024;
#endif
        std::cout << "{\"knobs\":" << count << ",\"requests\":" << requests
                  << ",\"coding\":\"" << coding << "\",\"payload_sha256\":\"" << digest
                  << "\",\"logical_bytes\":" << body.size() << ",\"response_wire_bytes\":" << wire_bytes
                  << ",\"wall_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end-wall_begin).count()
                  << ",\"caller_cpu_ns\":" << cpu_end-cpu_begin
                  << ",\"process_cpu_ns\":" << process_cpu(after)-process_cpu(before)
                  << ",\"request_median_ns\":" << latencies[latencies.size()/2]
                  << ",\"request_p99_ns\":" << latencies[(latencies.size()/100*99 + (latencies.size()%100*99+99)/100)-1]
                  << ",\"request_max_ns\":" << latencies.back()
                  << ",\"setup_peak_rss_bytes\":" << before.ru_maxrss*rss_scale
                  << ",\"process_peak_rss_bytes\":" << after.ru_maxrss*rss_scale
                  << ",\"minor_faults\":" << after.ru_minflt-before.ru_minflt
                  << ",\"major_faults\":" << after.ru_majflt-before.ru_majflt << "}\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
