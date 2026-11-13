// S5.2 T4(5): a power cut at every storage primitive of a run's seal commits, roll-up
// output groups, table publications (the first included), collector and an install,
// with the roll-up period changed mid-run. Recovery from the checkpoint the node could
// have persisted, then replay of the seals above it, reaches the directory of an
// uninterrupted run byte for byte; a rotted seal file is fetched from a peer.
#include "history.hh"
#include <cassert>
#include <iostream>
#include <journal/memory_io.h>
#include <map>

using namespace detent::authority;
namespace h = detent::authority::history;
namespace mem = kronuz::journal::testing;
namespace {
Context context() {
	Context c;
	c.cluster[0] = 'c';
	c.group[0] = 'g';
	c.epoch[0] = 'e';
	return c;
}
using Files = std::map<std::string, std::string>;
constexpr std::string_view prefix = "history/";
Files files(const mem::Disk &disk) {
	Files out;
	for (const auto &[name, id] : disk.names)
		if (name.starts_with(prefix))
			out[name.substr(prefix.size())] = disk.content(disk.inodes.at(id));
	return out;
}
std::shared_ptr<mem::MemoryIO> io(mem::Disk &disk) { return std::make_shared<mem::MemoryIO>(disk, std::string(prefix)); }
h::Fetch peer(const Files &source) {
	return [&source](std::string_view name) -> std::optional<std::string> {
		auto found = source.find(std::string(name));
		return found == source.end() ? std::nullopt : std::optional(found->second);
	};
}
// The states at every seal of one log, and the state before the enabling entry.
struct Log {
	State start{context()};
	std::vector<State> seals;
	Log() {
		State s(context());
		std::uint64_t index = 0, sequence = 0;
		auto entry = [&](std::uint64_t bytes) {
			if (s.close_entry(bytes))
				seals.push_back(s);
		};
		auto write = [&](const std::string &name, const std::string &key, std::optional<int> value) {
			Command c;
			c.context = context();
			c.producer = "p";
			c.generation = 1;
			c.sequence = ++sequence;
			c.name_space = name;
			c.mutations.push_back(
				{name + ":" + key, value ? std::optional(detent::KnobDef{*value}) : std::nullopt, {}, {}});
			s.apply(++index, c);
			entry(64);
		};
		Command open;
		open.context = context();
		open.kind = Kind::Open;
		open.producer = "p";
		open.nonce[0] = 'n';
		s.apply(++index, open);
		entry(0);
		for (int i = 0; i < 6; ++i)
			write(i % 2 ? "a" : "b", "k" + std::to_string(i), i);
		start = s;
		LayoutPolicy policy;
		policy.seal_transactions = 3;
		policy.period = 4;
		policy.carry_bytes = 48;
		policy.carry_count = 2;
		policy.alpha_num = 1;
		policy.alpha_den = 2;
		policy.log_entries = 64;
		AdministrativeCommand enable{context(), AdministrativeKind::EnablePositions, {}, {}, {}, policy};
		enable.nonce[0] = 's';
		assert(s.administer(++index, enable).status == Status::Applied);
		entry(64);
		const char *names[] = {"a", "b", "c", "d"};
		for (int i = 0; i < 120; ++i) {
			write(names[(i * 3) % 4], "k" + std::to_string((i * 5) % 9), i % 6 == 5 ? std::optional<int>() : i);
			if (i % 9 == 4) { // Log progress without positioned writes.
				for (int n = 0; n < 9; ++n) {
					s.advance(++index);
					entry(0);
				}
			}
			if (i == 50) { // The period changes at a seal the old period divides.
				auto changed = policy;
				changed.period = 8;
				changed.seal_transactions = 4;
				AdministrativeCommand set{context(), AdministrativeKind::SetLayout, {}, {}, {}, changed};
				assert(s.administer(++index, set).status == Status::Applied);
				entry(64);
			}
			if (i == 80) { // A drop, then a re-creation under a new incarnation.
				AdministrativeCommand drop{context(), AdministrativeKind::DropNamespace, {}, "c",
										   s.namespace_entry("c")->incarnation};
				assert(s.administer(++index, drop).status == Status::Applied);
				entry(64);
			}
		}
	}
};
// One run on `disk`: recovered from the checkpoint, then every seal above it. Returns
// the seal of the checkpoint a node could persist, every `every` durable seals.
struct Run {
	const Log &log;
	mem::Disk &disk;
	h::Fetch fetch;
	std::optional<std::size_t> checkpoint, installed;
	unsigned every = 1;
	void seal_from(h::History &history, std::size_t first) {
		for (auto k = first; k < log.seals.size(); ++k) {
			history.seal(log.seals[k]);
			if (history.durable_through(log.seals[k].applied_index()) && (k + 1) % every == 0)
				checkpoint = k;
		}
	}
	void recover(h::History &history) {
		history.recover(checkpoint ? log.seals[*checkpoint] : log.start);
	}
};
} // namespace

int main() {
	const Log log;
	assert(log.seals.size() > 30);
	unsigned tables = 0;
	for (const auto &s : log.seals)
		tables += s.seal().ordinal % s.policy().period == 0;
	assert(log.seals.back().policy().period == 8 && tables > 4);

	// The uninterrupted run, and the directory after every seal (a peer at that seal).
	mem::Disk reference;
	std::vector<Files> at;
	{
		h::History history(io(reference), {});
		history.recover(log.start);
		for (const auto &s : log.seals) {
			history.seal(s);
			at.push_back(files(reference));
		}
	}
	const auto expected = files(reference);
	const auto operations = reference.operations;
	assert(expected.contains("layout"));

	// A cut at every primitive of the run; recovery from the node's checkpoint.
	unsigned cuts = 0, below_table = 0;
	for (unsigned every : {1u, 3u})
		for (std::uint64_t cut = 1; cut <= operations; ++cut) {
			mem::Disk disk;
			disk.cut = cut;
			disk.random.seed(cut * every);
			Run run{log, disk, {}, {}, {}, every};
			try {
				h::History history(io(disk), {});
				run.recover(history);
				run.seal_from(history, 0);
			} catch (const mem::PowerLoss &) {
			}
			if (!disk.dead())
				continue;
			++cuts;
			disk.power_loss();
			h::History history(io(disk), {}); // No peer: what the node persisted suffices.
			run.recover(history);
			if (run.checkpoint && history.durable_table().index > log.seals[*run.checkpoint].applied_index())
				++below_table; // Replay re-seals what the table holds and folds none of it.
			run.seal_from(history, run.checkpoint ? *run.checkpoint + 1 : 0);
			assert(files(disk) == expected);
		}
	assert(cuts > 200 && below_table > 0);

	// An install mid-period from a peer at or past the snapshot's seal, cut at every primitive.
	unsigned installs = 0;
	for (std::size_t s : {std::size_t(9), std::size_t(14), log.seals.size() / 2})
		for (std::size_t ahead : {std::size_t(0), std::size_t(3)}) {
			const auto &source = at[std::min(s + ahead, at.size() - 1)];
			for (std::uint64_t cut = 1;; ++cut) {
				mem::Disk disk;
				disk.cut = cut;
				disk.random.seed(cut);
				Run run{log, disk, peer(source)};
				try {
					h::History history(io(disk), {}, peer(source));
					history.recover(log.start);
					history.install(log.seals[s]);
					run.checkpoint = run.installed = s; // Acknowledged: the checkpoint is the snapshot.
					run.seal_from(history, s + 1);
				} catch (const mem::PowerLoss &) {
				}
				if (!disk.dead()) {
					assert(files(disk) == expected);
					break;
				}
				++installs;
				disk.power_loss();
				h::History history(io(disk), {}, peer(source));
				run.recover(history);
				if (!run.installed)
					history.install(log.seals[s]); // Consensus sends the snapshot again.
				run.seal_from(history, run.checkpoint ? *run.checkpoint + 1 : s + 1);
				assert(files(disk) == expected);
			}
		}

	// A rotted seal file above the table, whose journal span is compacted, comes from a peer.
	std::size_t rotted = 0;
	{
		const auto table = h::decode_table(expected.at("layout"), {});
		for (std::size_t k = 0; k < log.seals.size(); ++k)
			if (log.seals[k].applied_index() > table.index)
				rotted = k;
		assert(rotted);
		auto name = h::seal_name(log.seals[rotted].applied_index());
		for (bool with_peer : {false, true}) {
			mem::Disk disk = reference;
			auto inode = disk.names.at(std::string(prefix) + name);
			disk.inodes.at(inode).durable[20] ^= 1;
			h::History history(io(disk), {}, with_peer ? peer(expected) : h::Fetch{});
			bool failed = false;
			try {
				history.recover(log.seals[rotted]);
			} catch (const std::runtime_error &) {
				failed = true;
			}
			assert(failed != with_peer);
			if (with_peer)
				assert(files(disk) == expected);
		}
	}
	std::cout << log.seals.size() << " seals, " << tables << " tables, " << operations << " storage operations: "
			  << cuts << " cuts (" << below_table << " recovered below the table), " << installs
			  << " cuts in installs, a rotted seal file fetched from a peer\n";
}
