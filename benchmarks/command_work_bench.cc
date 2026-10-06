// Monolithic command baseline. Run without other builds or tests:
// build/benchmarks/detent_command_work_bench
// Uses the public synchronous APIs as the independent continuation oracle.
#include "state.hh"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <time.h>

using namespace detent;
using namespace detent::authority;
namespace {
void check(bool condition, const char *message) {
	if (!condition)
		throw std::runtime_error(message);
}
double cpu() {
	timespec now{};
	check(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0, "thread CPU clock");
	return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}
Context context() {
	Context value;
	value.cluster[0] = 'c';
	value.group[0] = 'g';
	value.epoch[0] = 'e';
	return value;
}
Command open() {
	Command command;
	command.context = context();
	command.kind = Kind::Open;
	command.producer = "writer";
	command.nonce[0] = 'n';
	return command;
}
Command transaction(std::size_t count, const BoolDef &definition) {
	Command command;
	command.context = context();
	command.producer = "writer";
	command.generation = command.sequence = 1;
	command.name_space = "ns";
	for (std::size_t i = 0; i < count; ++i)
		command.mutations.push_back({"ns:feature_" + std::to_string(i), definition, {}, {}});
	return command;
}
Command heavy(std::size_t count) {
	BoolDef definition;
	std::optional<Command> last;
	for (std::size_t i = 0; i <= Limits{}.overrides; ++i) {
		auto command = transaction(count, definition);
		try {
			(void)encode(command);
		} catch (const std::invalid_argument &) {
			break;
		}
		last = std::move(command);
		definition.overrides["group" + std::to_string(i) + std::string(40, 'x')] = GroupRollout{25};
	}
	check(last.has_value(), "heavy command fits");
	return std::move(*last);
}
enum class Scenario { Applied, Unchanged, Conflict, Capacity, Duplicate, ReceiptHeavy, MaximumDefinition };
constexpr std::array names{"applied",	"unchanged",	 "conflict",		  "capacity",
						   "duplicate", "receipt_heavy", "maximum_definition"};
struct Sample {
	double decode_ms, digest_ms, apply_ms, cleanup_max_ms;
	std::size_t command_bytes;
};
Sample run(Scenario scenario, const Command &prototype) {
	Limits limits;
	if (scenario == Scenario::Capacity)
		limits.keys = 1;
	State source(context(), limits);
	source.apply(1, open());
	auto command = prototype;
	auto expected = Status::Applied;
	if (scenario == Scenario::Unchanged || scenario == Scenario::Duplicate) {
		check(source.apply(2, command).status == Status::Applied, "seed command applied");
		if (scenario == Scenario::Unchanged) {
			command.sequence = 2;
			expected = Status::NoChange;
		}
	} else if (scenario == Scenario::Conflict) {
		command.expected_namespace = 1;
		expected = Status::Conflict;
	} else if (scenario == Scenario::Capacity) {
		expected = Status::Capacity;
	} else if (scenario == Scenario::ReceiptHeavy) {
		for (std::uint64_t sequence = 1; sequence <= limits.results; ++sequence) {
			auto small = transaction(1, BoolDef{});
			small.sequence = sequence;
			small.mutations.front().key = "ns:receipt_seed";
			std::get<BoolDef>(*small.mutations.front().value).default_value = sequence & 1;
			check(source.apply(sequence + 1, small).status == Status::Applied, "receipt seed applied");
		}
		command.sequence = limits.results + 1;
	}
	const auto bytes = encode(command, limits);
	const auto expected_digest = digest(command, limits);
	Sample sample{};
	sample.command_bytes = bytes.size();
	auto start = cpu();
	auto decoded = decode(bytes, limits);
	sample.decode_ms = cpu() - start;
	check(encode(decoded, limits) == bytes, "canonical round trip");
	start = cpu();
	check(digest(decoded, limits) == expected_digest, "digest identity");
	sample.digest_ms = cpu() - start;
	auto candidate = source;
	start = cpu();
	auto result = candidate.apply(source.applied_index() + 1, decoded);
	sample.apply_ms = cpu() - start;
	check(result.status == expected, "scenario result");
	check(candidate.applied_index() == source.applied_index() + 1, "applied boundary");
	check(source.applied_index() < candidate.applied_index(), "source remains immutable");
	// Include bounded persistent-tree collection, whose individual payload
	// destructors currently have no separate record budget.
	candidate.retire();
	while (candidate.reclamation_pending()) {
		start = cpu();
		candidate.collect(32);
		sample.cleanup_max_ms = std::max(sample.cleanup_max_ms, cpu() - start);
	}
	return sample;
}
} // namespace
int main() {
	try {
		const auto wide = heavy(128), large = heavy(1);
		std::puts("scenario,command_bytes,mutations,overrides_per_definition,sample,decode_cpu_ms,digest_cpu_"
				  "ms,apply_cpu_ms,max_collect_cpu_ms");
		for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
			const auto &command = scenario == 6 ? large : wide;
			for (unsigned sample = 1; sample <= 5; ++sample) {
				auto timing = run(static_cast<Scenario>(scenario), command);
				std::printf("%s,%zu,%zu,%zu,%u,%.6f,%.6f,%.6f,%.6f\n", names[scenario], timing.command_bytes,
							command.mutations.size(),
							std::get<BoolDef>(*command.mutations.front().value).overrides.size(), sample,
							timing.decode_ms, timing.digest_ms, timing.apply_ms, timing.cleanup_max_ms);
			}
		}
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
