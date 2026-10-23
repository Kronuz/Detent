#include "../../benchmarks/local_corpus.hh"
#include <stdexcept>

namespace b = detent::bench;
namespace d = detent::distribution;
void check(bool value) {
	if (!value)
		throw std::runtime_error("scale corpus invariant");
}
int main() {
	check(b::local_key(999999, b::local_key_width(1000000)) == "key999999");
	check(b::local_key(0, b::local_key_width(1000001)) == "key0000000");
	check(b::local_key(1000000, b::local_key_width(1000001)) == "key1000000");
	check(b::local_key(9999999, b::local_key_width(10000000)) == "key9999999");
	check(b::local_key(999999, b::local_key_width(1000001)) <
		  b::local_key(1000000, b::local_key_width(1000001)));
	for (const auto count : {1u, 4u, 17u, 1000u}) {
		const auto corpus = b::local_corpus(count);
		d::SnapshotDecoder decoder(corpus.cursor, {"app"});
		auto remaining = std::span<const char>(corpus.wire);
		while (!remaining.empty()) {
			d::WorkBudget budget;
			decoder.feed(remaining, budget);
		}
		const auto snapshot = decoder.finish();
		check(snapshot.records().size() == count);
	}
	bool refused = false;
	try {
		b::local_key_width(0);
	} catch (const std::invalid_argument&) {
		refused = true;
	}
	check(refused);
}
