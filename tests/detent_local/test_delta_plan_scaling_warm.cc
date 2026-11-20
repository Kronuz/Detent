// Plans one delta more than once on one recovered generation (one mapping) and times each plan, to show
// what the first plan pays that later plans on the same mapping do not (first-touch page verification,
// first page faults). Not a ctest; docs/perf/delta-plan-scaling.md "A warm second plan".
//
//   delta_plan_warm CORPUS STORE FRACTION SEED [PLANS]
//
// STORE is a prepared store (namespace_measure_t9 `full`). FRACTION is the T9 arm (0.05, c0.05, 0.5).
// PLANS is a string over A and B (default AAB): A is the delta of SEED, B the same shape with SEED + 1.
// The first letter's plan is cold; each later one runs on the mapping the earlier ones touched. Nothing is
// published, so every plan has the same base. One JSON line per plan on stdout.
#define main namespace_measure_t9_unused_main
#include "../../docs/perf/namespace-measure-t9.cc"
#undef main

namespace {
struct Prepared {
	Framed framed;
	std::uint64_t raw = 0, keys = 0;
};

Prepared prepare_delta(const Records &recs, const d::Cursor &cursor, double fraction, bool clustered,
					   std::uint64_t seed) {
	auto answer = delta_wire(recs, cursor, fraction, clustered, seed);
	Prepared p;
	p.framed = frame(d::UpdateKind::Delta, answer.wire);
	p.raw = answer.wire.size();
	p.keys = answer.keys;
	return p;
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc < 5 || argc > 6)
			throw std::invalid_argument("usage: CORPUS STORE FRACTION SEED [PLANS]");
		const std::filesystem::path corpus_path = argv[1], store_path = argv[2];
		const bool clustered = argv[3][0] == 'c';
		const double fraction = std::stod(argv[3] + (clustered ? 1 : 0));
		const std::uint64_t seed = std::stoull(argv[4]);
		const std::string sequence = argc == 6 ? argv[5] : "AAB";
		if (!(fraction > 0 && fraction <= 1) || sequence.empty() ||
			sequence.find_first_not_of("AB") != std::string::npos)
			throw std::invalid_argument("fraction in (0, 1]; PLANS over A and B");
		std::vector<std::string> scope;
		d::Cursor cursor;
		Prepared answers[2];
		{
			const auto corpus = load(corpus_path);
			const auto recs = records(corpus);
			cursor = cursor_at(recs.cursor_revision);
			scope = recs.scope;
			for (int i = 0; i < 2; ++i)
				answers[i] = prepare_delta(recs, cursor, fraction, clustered, seed + i);
		}
		Store store(store_path);
		const auto selected = store.disk->selection().descriptor;
		if (!selected || selected->cursor.revision != cursor.revision)
			throw std::runtime_error("store does not hold the corpus at its revision");
		const auto r0 = Clock::now();
		const auto actual = store.actual();
		const double recover = seconds(Clock::now() - r0);
		const auto bounds = l::detail::paged_limits(store.options.recovery_limits, l::paged::root_bytes);
		bool seen[2] = {false, false};
		for (std::size_t n = 0; n < sequence.size(); ++n) {
			const int which = sequence[n] - 'A';
			const auto &a = answers[which];
			const auto start = Clock::now();
			d::DeltaDecoder decoder(cursor, scope, wire_limits(store.heap), d::UpdateKind::Delta);
			unframe(a.framed, d::UpdateKind::Delta, a.raw,
					[&](std::span<const char> &in, d::WorkBudget &b) { decoder.feed(in, b); }, store.heap);
			auto source = std::make_shared<const d::ValidatedDelta>(decoder.finish());
			const double decode = seconds(Clock::now() - start);
			const auto planned = Clock::now();
			const auto plan_cpu = cpu_seconds();
			l::detail::PagedDeltaPlanBuilder builder(*actual, source, store.domain.factory(), store.heap, bounds);
			while (!builder.done()) {
				l::ValidationBudget budget;
				builder.step(budget);
			}
			auto plan = builder.take();
			const double plan_s = seconds(Clock::now() - planned), plan_cpu_s = cpu_seconds() - plan_cpu;
			std::uint64_t plan_bytes = 0;
			for (const auto &built : plan.segments())
				plan_bytes += built.bytes->size();
			std::cout << "{\"arm\":\"warm\",\"fraction\":" << fraction << ",\"shape\":\""
					  << (clustered ? "clustered" : "uniform") << "\",\"plan_index\":" << n + 1
					  << ",\"delta\":\"" << sequence[n] << "\",\"role\":\""
					  << (n == 0 ? "cold" : seen[which] ? "warm_same" : "warm_other")
					  << "\",\"keys\":" << a.keys << ",\"decode_s\":" << decode << ",\"plan_s\":" << plan_s
					  << ",\"plan_cpu_s\":" << plan_cpu_s << ",\"plan_bytes\":" << plan_bytes
					  << ",\"recover_s\":" << recover << ",\"store_open_s\":" << store.open_seconds
					  << ",\"peak_rss\":" << peak_rss() << "}" << std::endl;
			seen[which] = true;
		}
	} catch (const std::exception &e) {
		std::cerr << "delta_plan_warm: " << e.what() << '\n';
		return 1;
	}
}
