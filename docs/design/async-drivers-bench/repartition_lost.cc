// Review probe R1 (async-drivers-design, design lens), with D10's fix, over journal's real
// RFTW0001 turn log. Old generation: journal's own damage() scenario (test/turn_log_test.cc),
// after which groups 1 and 6 are lost and must rejoin under new ids. The open marks the groups
// recovery calls lost in the host's group table and syncs it, then re-partitions from one loop to
// two: each recovered group's live records into its new loop's log of the next generation, then
// the layout record. It reopens after a crash before the commit and after the commit, and asks
// whether the old id 1 may append from sequence 1 again. The host opens each TurnLog with the
// table's lost ids (quest 3); at the pin TurnLog rebuilds `lost` from its own recovery only, so
// the host's refusal stands in for that seeding here. Exits 1 if the old id appends again.
//   repartition_lost [forget]   `forget`: the control, no lost marks (D10 at Detent:1ece2ff)
#include "journal/turn_log.h"
#include "completion/posix.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

namespace j = kronuz::journal;
namespace fs = std::filesystem;

namespace {
const j::Identity identity{'T', 'U', 'R', 'N'};
const j::TurnLogOptions small{8 * 512, 512, 1};
bool forget = false;

fs::path dir(const fs::path& p) {
	fs::create_directories(p);
	fs::permissions(p, fs::perms::owner_all, fs::perm_options::replace);
	return p;
}
j::TurnRecord record(std::uint64_t group, std::uint64_t sequence, std::size_t size) {
	std::string payload(size, '\0');
	for (std::size_t i = 0; i < size; ++i) { payload[i] = static_cast<char>('a' + (group * 7 + sequence * 3 + i) % 26); }
	return {group, sequence, j::TurnRecordKind::Batch, payload};
}
template <class F> bool throws(F&& f) {
	try { f(); return false; } catch (const std::exception&) { return true; }
}
std::uint32_t loop_of(std::uint64_t id, std::uint32_t L) {
	const auto slot = static_cast<std::uint32_t>((id * 0x9E3779B97F4A7C15ull) >> 52);
	return static_cast<std::uint32_t>((std::uint64_t{slot} * L) >> 12);
}

// A small durable file, replaced whole: written beside, synced, renamed, the directory synced.
void replace(const fs::path& path, const std::string& bytes) {
	const auto tmp = path.string() + ".tmp";
	const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || ::write(fd, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()) || ::fsync(fd) != 0) { std::abort(); }
	::close(fd);
	fs::rename(tmp, path);
	const int d = ::open(path.parent_path().c_str(), O_RDONLY);
	::fsync(d);
	::close(d);
}
// The log directory's owner: owner.lock is created with the log and opened ever after.
std::unique_ptr<j::OwnerLock> own(j::PosixIO& io, const fs::path& d) { return io.acquire_owner(!fs::exists(d / "owner.lock")); }
std::string slurp(const fs::path& path) {
	std::ifstream in(path);
	return {std::istreambuf_iterator<char>(in), {}};
}

// The host's durable state beside the turn logs: the layout record names (L, generation); the
// group table's lost marks name each incarnation recovery called lost.
struct host {
	fs::path root;
	std::uint32_t L = 1, generation = 1;
	std::set<std::uint64_t> lost;
	void load() {
		std::sscanf(slurp(root / "layout").c_str(), "%u %u", &L, &generation);
		lost.clear();
		const std::string table = slurp(root / "groups");
		for (std::size_t at = 0; at < table.size();) {
			char* end = nullptr;
			lost.insert(std::strtoull(table.c_str() + at, &end, 10));
			at = static_cast<std::size_t>(end - table.c_str()) + 1;
		}
	}
	void mark(const std::set<std::uint64_t>& ids) {
		std::string table;
		for (auto id : ids) { table += std::to_string(id) + "\n"; }
		replace(root / "groups", table);
		lost = ids;
	}
	void commit(std::uint32_t l, std::uint32_t g) {
		replace(root / "layout", std::to_string(l) + " " + std::to_string(g) + "\n");
		L = l;
		generation = g;
	}
	fs::path log(std::uint32_t g, std::uint32_t loop) const { return dir(root / ("gen" + std::to_string(g) + "-loop" + std::to_string(loop))); }
};

// One open of the stored generation: recovers each loop's log, marks what recovery lost (unless
// `forget`), tries old id 1 at sequence 1 as a rejoin that forgot it would, and, when `L`
// differs, re-partitions; `crash` stops before the commit.
struct opened {
	std::uint32_t generation = 0;
	std::set<std::uint64_t> refused;  // what the host refuses: recovery's lost ids and its table's marks
	bool log_lost = false;            // whether TurnLog's own recovery calls group 1 lost
	bool accepted = false;            // old id 1 appended at sequence 1
};
opened open(host& h, std::uint32_t L, bool crash) {
	h.load();
	std::set<std::uint64_t> lost = h.lost;
	std::map<std::uint64_t, std::vector<j::TurnRecord>> live;
	opened o{h.generation};
	for (std::uint32_t loop = 0; loop < h.L; ++loop) {
		const auto d = h.log(h.generation, loop);
		j::PosixIO io(d);
		auto owner = own(io, d);
		j::TurnRecovery recovery;
		j::TurnLog log(io, identity, recovery, small);
		lost.insert(recovery.lost.begin(), recovery.lost.end());
		for (const auto& [g, records] : recovery.groups) { live[g] = records; }
		if (loop == loop_of(1, h.L)) { o.log_lost = recovery.lost.contains(1); }
	}
	if (!forget && lost != h.lost) { h.mark(lost); }
	o.refused = lost;
	if (!lost.contains(1)) {  // the host admits the old id: it appends from sequence 1
		const auto d = h.log(h.generation, loop_of(1, h.L));
		j::PosixIO io(d);
		auto owner = own(io, d);
		j::TurnRecovery recovery;
		j::TurnLog log(io, identity, recovery, small);
		o.accepted = !throws([&] { log.append(std::vector{record(1, 1, 10)}); });
		if (o.accepted) { live[1] = {record(1, 1, 10)}; }
	}
	if (L != h.L) {
		const std::uint32_t next = h.generation + 1;
		for (std::uint32_t loop = 0; loop < 4; ++loop) {  // a re-partition that did not commit starts again
			fs::remove_all(h.root / ("gen" + std::to_string(next) + "-loop" + std::to_string(loop)));
		}
		for (std::uint32_t loop = 0; loop < L; ++loop) {  // each new log: its groups' live records, synced per turn
			const auto d = h.log(next, loop);
			j::PosixIO io(d);
			auto owner = own(io, d);
			j::TurnRecovery fresh;
			j::TurnLog log(io, identity, fresh, small);
			for (const auto& [g, records] : live) {
				if (loop_of(g, L) != loop) { continue; }
				for (const auto& r : records) { log.append(std::vector{r}); }
			}
		}
		if (!crash) {
			h.commit(L, next);
			for (std::uint32_t loop = 0; loop < 4; ++loop) { fs::remove_all(h.root / ("gen" + std::to_string(next - 1) + "-loop" + std::to_string(loop))); }
		}
	}
	return o;
}
}  // namespace

int main(int argc, char** argv) {
	forget = argc > 1 && std::string_view(argv[1]) == "forget";
	const fs::path root = fs::path(std::getenv("BENCH_DIR") ? std::getenv("BENCH_DIR") : ".") / "repartition-lost";
	fs::remove_all(root);
	host h{dir(root)};
	h.commit(1, 1);
	h.mark({});
	{
		const auto old_dir = h.log(1, 0);
		j::PosixIO io(old_dir);
		auto owner = own(io, old_dir);
		std::uint64_t at = 0;
		{
			j::TurnRecovery recovery;
			j::TurnLog log(io, identity, recovery, small);
			log.append(std::vector{record(4, 1, 20)});
			log.append(std::vector{record(6, 1, 700)});
			at = log.position();
			log.append(std::vector{record(1, 1, 300), record(2, 1, 300)});
			log.append(std::vector{record(3, 1, 300)});
			log.append(std::vector{record(1, 2, 300)});
		}
		// as damage(): flip a byte inside turn 3's first fragment header
		std::fstream f(old_dir / j::turn_log_detail::segment_name(1), std::ios::in | std::ios::out | std::ios::binary);
		f.seekg(static_cast<std::streamoff>(at + 10));
		char c;
		f.get(c);
		f.seekp(static_cast<std::streamoff>(at + 10));
		f.put(static_cast<char>(c ^ 0x5a));
	}
	bool ok = true;
	auto report = [&](const char* what, const opened& o) {
		std::printf("%s: recovered generation=%u refused={", what, o.generation);
		for (auto g : o.refused) { std::printf(" %llu", static_cast<unsigned long long>(g)); }
		std::printf(" } turn_log_recovery_calls_1_lost=%d; old id 1 at sequence 1: %s; now generation=%u L=%u\n", int(o.log_lost),
			o.accepted ? "ACCEPTED" : "refused", h.generation, h.L);
		ok = ok && !o.accepted;
	};
	report("open L=1 over the damaged generation", open(h, 1, false));
	report("open L=2, crash before the commit", open(h, 2, true));
	report("open L=2, re-partition 1->2", open(h, 2, false));
	report("reopen L=2", open(h, 2, false));
	report("open L=3, re-partition 2->3", open(h, 3, false));
	report("reopen L=3", open(h, 3, false));
	fs::remove_all(root);
	std::printf("repartition_lost%s: %s\n", forget ? " (no lost marks)" : "",
		ok ? "lost verdict survives the re-partition ok" : "FAILS: the re-partition forgets that group 1 was lost; its old id appends from sequence 1");
	return ok ? 0 : 1;
}
