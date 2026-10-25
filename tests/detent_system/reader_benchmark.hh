#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>
#include <random>
#include <sys/resource.h>
#include <vector>

class ReaderBenchmark {
  public:
	explicit ReaderBenchmark(int &argc, char **argv) {
		if (argc < 4 || std::string_view(argv[argc - 3]) != "--benchmark")
			return;
		calls_ = parse(argv[argc - 2]);
		capacity_ = parse(argv[argc - 1]);
		argc -= 3;
	}
	bool enabled() const noexcept { return calls_ != 0; }
	void prepare(std::uint64_t records) {
		if (enabled()) {
			capacity_ = std::min(capacity_, records);
			queries_.reserve(capacity_);
		}
	}
	void add(std::string_view key, bool boolean, std::int64_t value, bool rollout = false) {
		if (!enabled())
			return;
		const auto slot = seen_ < capacity_ ? seen_ : random_() % (seen_ + 1);
		++seen_;
		if (slot >= capacity_)
			return;
		Query query{std::string(key), boolean, value, rollout};
		if (slot == queries_.size())
			queries_.push_back(std::move(query));
		else
			queries_[slot] = std::move(query);
	}
	template <class Client, class View> void run(const Client &client, const View &view) const {
		if (!enabled())
			return;
		for (unsigned kind = 0; kind != 3; ++kind) {
			std::vector<const Query *> selected;
			for (const auto &query : queries_)
				if (query.boolean == (kind != 0))
					selected.push_back(&query);
			if (selected.empty())
				continue;
			for (bool hot : {true, false}) {
				for (unsigned repetition = 0; repetition != 4; ++repetition) {
					if (repetition % 2) {
						measure(view, selected, kind, hot, "pinned-view", repetition);
						measure(client, selected, kind, hot, "client", repetition);
					} else {
						measure(client, selected, kind, hot, "client", repetition);
						measure(view, selected, kind, hot, "pinned-view", repetition);
					}
				}
			}
		}
	}

	static rusage usage() {
		rusage value{};
		if (::getrusage(RUSAGE_SELF, &value) != 0)
			throw std::runtime_error("reader benchmark resource usage unavailable");
		return value;
	}
	static double cpu(const rusage &value) {
		return value.ru_utime.tv_sec + value.ru_stime.tv_sec +
			   (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1e6;
	}

  private:
	struct Query { std::string key; bool boolean; std::int64_t value; bool rollout; };
	static std::uint64_t parse(std::string_view text) {
		std::uint64_t value = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (error != std::errc{} || end != text.data() + text.size() || !value || value > SIZE_MAX)
			throw std::invalid_argument("benchmark requires positive lookup and query counts");
		return value;
	}
	template <class Reader>
	void measure(const Reader &reader, const std::vector<const Query *> &queries,
				 unsigned kind, bool hot, const char *path, unsigned repetition) const {
		auto lookup = [&](const Query &query) {
			bool correct;
			if (!kind) {
				const auto result = reader.get(query.key);
				correct = result && result.value() == query.value;
			} else {
				const auto result = kind == 1 ? reader.eval(query.key)
					: reader.eval(query.key, std::string_view("cohort-A"));
				correct = result && result.value() == (kind == 1 ? bool(query.value) : query.rollout);
			}
			if (!correct)
				throw std::runtime_error("reader benchmark result mismatch: " + query.key);
		};
		for (std::size_t index = 0; index != (hot ? 1 : queries.size()); ++index)
			lookup(*queries[index]);
		std::vector<std::int64_t> latency(calls_);
		const auto before = usage();
		const auto begin = std::chrono::steady_clock::now();
		for (std::size_t index = 0; index != calls_; ++index) {
			const auto &query = *queries[hot ? 0 : index % queries.size()];
			const auto start = std::chrono::steady_clock::now();
			lookup(query);
			const auto end = std::chrono::steady_clock::now();
			latency[index] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		}
		const auto end = std::chrono::steady_clock::now();
		const auto after = usage();
		std::sort(latency.begin(), latency.end());
		auto percentile = [&](unsigned percent) {
			return latency[(calls_ / 100 * percent + (calls_ % 100 * percent + 99) / 100) - 1];
		};
		std::cout << "{\"kind\":\"reader_benchmark\",\"path\":\"" << path
			<< "\",\"operation\":\"" << (kind == 0 ? "integer" : kind == 1 ? "boolean" : "cohort-eval")
			<< "\",\"working_set\":\"" << (hot ? "hot" : "scattered")
			<< "\",\"repetition\":" << repetition
			<< ",\"queries\":" << (hot ? 1 : queries.size()) << ",\"calls\":" << calls_
			<< ",\"wall_seconds\":" << std::chrono::duration<double>(end - begin).count()
			<< ",\"process_cpu_seconds\":" << cpu(after) - cpu(before)
			<< ",\"p50_ns\":" << percentile(50) << ",\"p95_ns\":" << percentile(95)
			<< ",\"p99_ns\":" << percentile(99) << ",\"maximum_ns\":" << latency.back()
			<< ",\"process_minor_faults\":" << after.ru_minflt - before.ru_minflt
			<< ",\"process_major_faults\":" << after.ru_majflt - before.ru_majflt << "}\n";
	}
	std::uint64_t calls_ = 0, capacity_ = 0, seen_ = 0;
	std::mt19937_64 random_{0x524541444552};
	std::vector<Query> queries_;
};
