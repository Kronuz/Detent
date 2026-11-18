#pragma once
// A namespace's history from a list of transactions, tiled into a cut, spans and a ring as a test
// says: the test history source of the W5/W6 tests (namespace-s6).
#include "detent_distribution/history.hh"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace history_fixture {
namespace d = detent::distribution;
using detent::KnobDef;
inline d::Identity id(char byte) {
	d::Identity result;
	result.fill(byte);
	return result;
}
struct Write {
	std::string key;
	std::optional<KnobDef> value; // empty: a delete
};
using Transaction = std::vector<Write>;
struct Held {
	std::uint64_t revision = 0;
	std::optional<KnobDef> value;
	bool operator==(const Held &) const = default;
};
using State = std::map<std::string, Held>; // tombstones held only inside spans

class VectorReader final : public d::HistoryReader {
  public:
	explicit VectorReader(std::vector<std::pair<std::string, Held>> rows) : rows_(std::move(rows)) {}
	const d::HistoryRecord *next() override {
		if (index_ == rows_.size())
			return nullptr;
		const auto &[key, held] = rows_[index_++];
		record_ = {key, held.revision, held.value ? &*held.value : nullptr};
		return &record_;
	}

  private:
	std::vector<std::pair<std::string, Held>> rows_;
	std::size_t index_ = 0;
	d::HistoryRecord record_;
};

// A namespace's history from its transactions, tiled as the test says.
class TestHistory : public d::NamespaceHistory {
  public:
	TestHistory(std::vector<Transaction> transactions, std::uint64_t cut, std::vector<d::HistorySpan> spans,
				std::uint64_t ring_start, d::Identity incarnation = id(2))
		: transactions_(std::move(transactions)), cut_(cut), ring_start_(ring_start), spans_(std::move(spans)),
		  incarnation_(incarnation) {}
	d::NamespaceCursor head() const noexcept override { return {id(1), incarnation_, transactions_.size()}; }
	std::uint64_t cut() const noexcept override { return cut_; }
	std::uint64_t ring_start() const noexcept override { return ring_start_; }
	std::span<const d::HistorySpan> spans() const noexcept override { return spans_; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t index) const override {
		return std::make_unique<VectorReader>(rows(span(spans_[index].first, spans_[index].last)));
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t after) const override {
		std::vector<std::pair<std::string, Held>> out;
		for (auto position = after + 1; position <= transactions_.size(); ++position) {
			auto ordered = transactions_[position - 1];
			std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.key < b.key; });
			for (auto &write : ordered)
				out.emplace_back(write.key, Held{position, write.value});
		}
		return std::make_unique<VectorReader>(std::move(out));
	}
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		return std::make_unique<VectorReader>(rows(state_at(ring_start_)));
	}
	State span(std::uint64_t first, std::uint64_t last) const {
		State out;
		for (auto position = first; position <= last; ++position)
			for (const auto &write : transactions_[position - 1])
				out[write.key] = {position, write.value};
		return out;
	}
	State state_at(std::uint64_t position) const {
		State out;
		for (auto &[key, held] : span(1, position))
			if (held.value)
				out[key] = held;
		return out;
	}
	static std::vector<std::pair<std::string, Held>> rows(const State &state) { return {state.begin(), state.end()}; }

  protected:
	std::vector<Transaction> transactions_;
	std::uint64_t cut_, ring_start_;
	std::vector<d::HistorySpan> spans_;
	d::Identity incarnation_;
};

inline KnobDef def(std::int64_t value, int shape) {
	KnobDef out;
	out.default_value = value;
	if (shape % 3 == 1) {
		out.rollout_pct = static_cast<std::uint8_t>(value % 101);
		out.rollout_value = -value;
	} else if (shape % 3 == 2)
		out.overrides.emplace("group-" + std::to_string(value % 4), std::int64_t(value * 7));
	return out;
}
} // namespace history_fixture
