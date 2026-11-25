// The checkpoint and two snapshot exports of one seeded authority state, whose maps take
// sets, erases and namespace drops (range erasure). With a directory argument it writes
// checkpoint.bin, export-0.bin and export-1.bin there, for a byte comparison across builds.
#include "export_encoder.hh"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace a = detent::authority;
namespace d = detent::distribution;
namespace {
void check(bool value, const char *what) {
	if (!value)
		throw std::runtime_error(what);
}
a::Context context() {
	a::Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
std::string encode(a::SnapshotExportEncoder &encoder) {
	std::string result;
	while (!encoder.done()) {
		d::WorkBudget budget{4096, 16};
		encoder.step(budget);
		if (encoder.chunk_ready())
			result.append(encoder.take_chunk()->view());
	}
	return result;
}
std::string snapshot(const a::State &state, std::vector<std::string> names) {
	std::set<std::string> allowed(names.begin(), names.end());
	auto account = std::make_shared<d::StorageLedger>(64 * 1024 * 1024);
	auto capture = state.capture(a::AuthorizedScope::authorize("reader", 1, names, allowed, account));
	a::SnapshotExportEncoder encoder(capture);
	auto bytes = encode(encoder);
	check(encoder.encoded_length() == bytes.size(), "export length");
	return bytes;
}
void write(const std::string &directory, const char *name, const std::string &bytes) {
	std::ofstream file(directory + "/" + name, std::ios::binary);
	file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	check(bool(file), "capture write");
}
} // namespace
int main(int argc, char **argv) {
	try {
		a::State state(context());
		std::uint64_t index = 0;
		a::Command command;
		command.context = context();
		command.producer = "writer";
		command.kind = a::Kind::Open;
		command.nonce[0] = 'n';
		check(state.apply(++index, command).status == a::Status::Applied, "open");
		command.kind = a::Kind::Transaction;
		command.nonce = {};
		command.generation = 1;
		auto transact = [&](std::string name, std::vector<a::Mutation> mutations) {
			command.name_space = std::move(name);
			command.mutations = std::move(mutations);
			++command.sequence;
			check(state.apply(++index, command).status == a::Status::Applied, "transaction");
		};
		auto name = [](unsigned n) { return "ns" + std::to_string(n % 24); };
		auto key = [&](unsigned n) { return name(n) + ":service/feature_" + std::to_string(n); };
		auto definition = [](unsigned n) {
			detent::KnobDef value{static_cast<std::int64_t>(n)};
			if (n % 3 == 0) {
				value.rollout_pct = static_cast<std::uint8_t>(n % 101);
				value.rollout_value = 1;
				value.overrides.emplace("group" + std::to_string(n % 7), detent::GroupRollout{5});
				value.overrides.emplace("beta", std::int64_t(n % 2));
			}
			return value;
		};
		for (unsigned n = 0; n < 2400; n += 240) { // Ten keys of each of the 24 namespaces per round.
			for (unsigned space = 0; space < 24; ++space) {
				std::vector<a::Mutation> rows;
				for (unsigned i = n + space; i < n + 240; i += 24)
					rows.push_back({key(i), definition(i), {}, {}});
				transact(name(space), std::move(rows));
			}
		}
		for (unsigned n = 0; n < 2400; n += 24) // One namespace, ns0, rewritten and thinned.
			transact(name(n), {{key(n), definition(n + 1), {}, {}}, {key(n + 24 * 50), std::nullopt, {}, {}}});
		a::AdministrativeCommand enable{context(), a::AdministrativeKind::EnablePositions, {}, {}, {}};
		enable.nonce[0] = 'p';
		check(state.administer(++index, enable).status == a::Status::Applied, "enable positions");
		for (unsigned dropped : {3u, 11u, 17u}) {
			auto entry = state.namespace_entry(name(dropped));
			check(entry && entry->live, "drop target");
			a::AdministrativeCommand drop{context(), a::AdministrativeKind::DropNamespace, {}, name(dropped),
										  entry->incarnation};
			check(state.administer(++index, drop).status == a::Status::Applied, "drop");
		}
		transact(name(11), {{key(11), definition(11), {}, {}}}); // A new incarnation.
		check(state.size() == 2400 - 50 - 3 * 100 + 1, "record count");

		const auto image = state.checkpoint();
		check(a::State::restore(image, context()).checkpoint() == image, "checkpoint round trip");
		const auto first = snapshot(state, {"ns1", "ns11", "ns2", "ns3", "ns0"});
		std::vector<std::string> names;
		for (unsigned n = 0; n < 24; ++n)
			names.push_back(name(n));
		const auto second = snapshot(state, names);
		check(first.size() < second.size(), "export sizes");
		if (argc > 1) {
			write(argv[1], "checkpoint.bin", image);
			write(argv[1], "export-0.bin", first);
			write(argv[1], "export-1.bin", second);
		}
		std::cout << "checkpoint " << image.size() << " bytes, exports " << first.size() << " and "
				  << second.size() << " bytes\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
