// namespace-delivery D8 "Catch-up input" and S7: an agent at R applies each of S6's three answers (the
// ring entries after R, the block trimmed above R, a base) through the real paged builders, and every
// one yields the full copy's state at h, byte for byte in the state commitment (the root page's digest
// over every namespace revision, key, revision and value). The block carries a tombstone for a key
// created and deleted after R, which the base never held: the agent applies it as a no-op.
#include "../detent_local/paged_disk_fixture.hh"
#include "detent_local/agent_store.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_delta_plan.hh"
#include "paged_plan.hh"
#include "paged_snapshot_plan.hh"
#include <map>
#include <optional>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;

namespace {
using Value = std::optional<std::int64_t>; // nullopt: a delete (a tombstone in a block)
struct Change {
	std::uint64_t revision;
	std::string key;
	Value value;
};
d::Cursor at(std::uint64_t revision) {
	auto value = cursor();
	value.revision = revision;
	return value;
}
void put_cursor(std::string &out, const d::Cursor &c) {
	m::array(out, 5);
	for (const auto *id : {&c.cluster, &c.group, &c.restore_epoch})
		m::string(out, {id->data(), id->size()}, true);
	m::string(out, {c.scope.data(), c.scope.size()}, true);
	m::unsigned_integer(out, c.revision);
}
void put_definition(std::string &out, std::int64_t value) {
	m::array(out, 4); // [default, rollout %, rollout value, overrides]
	m::signed_integer(out, value);
	out.push_back('\xc0');
	out.push_back('\xc0');
	m::array(out, 0);
}
// A full copy of namespace "app" at `revision`: its records are (key, revision, value).
std::shared_ptr<const d::ValidatedSnapshot> full_copy(std::uint64_t revision, std::uint64_t namespace_revision,
													  const std::map<std::string, std::pair<std::uint64_t, std::int64_t>> &records) {
	std::string out;
	std::uint64_t strings = 3;
	for (const auto &[key, record] : records)
		strings += key.size();
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	put_cursor(out, at(revision));
	m::array(out, 4);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, records.size());
	m::unsigned_integer(out, 0);
	m::unsigned_integer(out, strings);
	m::array(out, 1);
	m::array(out, 2);
	m::string(out, "app");
	m::unsigned_integer(out, namespace_revision);
	m::array(out, std::uint32_t(records.size()));
	for (const auto &[key, record] : records) {
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		m::string(out, key);
		m::unsigned_integer(out, record.first);
		put_definition(out, record.second);
	}
	return std::make_shared<const d::ValidatedSnapshot>(decode(out, at(revision)));
}
// Changes after `base` up to `target`, one transaction per revision, keys sorted within it: the ring
// entries are such a delta, and so is a block (each key's last change, grouped by its revision).
d::ValidatedDelta changes(std::uint64_t base, std::uint64_t target, std::vector<Change> list,
						  std::shared_ptr<d::StorageLedger> account) {
	std::sort(list.begin(), list.end(),
			  [](const Change &a, const Change &b) { return std::tie(a.revision, a.key) < std::tie(b.revision, b.key); });
	std::map<std::uint64_t, std::vector<const Change *>> transactions;
	std::uint64_t strings = 0;
	for (const auto &change : list) {
		auto &tx = transactions[change.revision];
		if (tx.empty())
			strings += 3;
		tx.push_back(&change);
		strings += change.key.size();
	}
	std::string out;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 2);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	put_cursor(out, at(base));
	put_cursor(out, at(target));
	m::array(out, 4);
	m::unsigned_integer(out, transactions.size());
	m::unsigned_integer(out, list.size());
	m::unsigned_integer(out, 0);
	m::unsigned_integer(out, strings);
	m::array(out, std::uint32_t(transactions.size()));
	for (const auto &[revision, tx] : transactions) {
		m::array(out, 3);
		m::unsigned_integer(out, revision);
		m::array(out, 1);
		m::array(out, 2);
		m::string(out, "app");
		m::unsigned_integer(out, revision);
		m::array(out, std::uint32_t(tx.size()));
		for (const auto *change : tx) {
			m::array(out, 4);
			m::unsigned_integer(out, 0);
			m::string(out, change->key);
			m::unsigned_integer(out, change->value ? 0 : 1);
			if (change->value)
				put_definition(out, *change->value);
			else
				out.push_back('\xc0');
		}
	}
	d::Limits limits;
	limits.storage_account = std::move(account);
	d::DeltaDecoder decoder(at(base), {"app"}, limits);
	auto input = std::span<const char>(out);
	while (!input.empty()) {
		d::WorkBudget budget{64, 16};
		decoder.feed(input, budget);
	}
	return decoder.finish();
}
template <class Builder> auto drive(Builder &builder) {
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	return builder.take();
}
// The authority's history after R = 10, up to h = 14: app:e is created and deleted after R.
constexpr std::uint64_t R = 10, h = 14;
const std::vector<Change> ring{
	{11, "app:b", 20}, {11, "app:d", 4},  {12, "app:a", {}}, {12, "app:e", 5},
	{13, "app:c", 30}, {13, "app:e", {}}, {14, "app:b", 21},
};
// Each key's last change since R: app:e's is a tombstone for a key the store never held.
const std::vector<Change> block{
	{11, "app:d", 4}, {12, "app:a", {}}, {13, "app:c", 30}, {13, "app:e", {}}, {14, "app:b", 21},
};
std::shared_ptr<const d::ValidatedSnapshot> base_copy() {
	return full_copy(R, 7, {{"app:a", {3, 1}}, {"app:b", {5, 2}}, {"app:c", {7, 3}}});
}
std::shared_ptr<const d::ValidatedSnapshot> target_copy() {
	return full_copy(h, h, {{"app:b", {14, 21}}, {"app:c", {13, 30}}, {"app:d", {11, 4}}});
}
// The agent's store, opened as the agent opens it, with what it needs to build and publish.
struct Agent {
	std::shared_ptr<d::StorageLedger> account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
	std::shared_ptr<d::StorageLedger> baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime{baseline, 1};
	d::RetirementDomain domain = runtime.domain(account, 64);
	l::DiskStoreOptions options;
	std::shared_ptr<l::DiskStore> store;
	explicit Agent(const std::filesystem::path &directory) {
		options.limits = {64 * l::paged::segment_bytes, 1000, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		require(store->status() == l::StoreStatus::Ready);
	}
	~Agent() {
		store->close();
		drive_store();
		store.reset();
		domain.close().wait();
		runtime.shutdown();
	}
	void drive_store() {
		for (unsigned turns = 0;; ++turns) {
			require(turns < 100000);
			store->advance();
			auto work = store->work();
			if (!work)
				break;
			work->run();
		}
	}
	l::RecoveredGeneration recover(const std::filesystem::path &directory) {
		return l::detail::ManagedRecovery::recover(directory, l::detail::ManagedRecovery::capture(*store));
	}
	// Builds `delta` on the selected generation and publishes it, confirming its recovery.
	l::paged::Digest publish(const std::filesystem::path &directory, d::ValidatedDelta delta) {
		std::optional<l::RecoveredGeneration> selected(recover(directory));
		l::detail::PagedDeltaPlanBuilder builder(*selected, std::move(delta), domain);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		store->begin(builder.take());
		drive_store();
		require(store->publication_status() == l::DiskPublicationStatus::Published);
		selected.reset();
		auto recovered = recover(directory);
		store->confirm_recovered(recovered);
		drive_store();
		return state_of(recovered);
	}
	static l::paged::Digest state_of(const l::RecoveredGeneration &generation) {
		return l::paged::state_commitment(l::detail::RecoveredTesting::root(generation).root());
	}
};
// The child of a cut: publishes the ring entries and dies after the store's `cut`th primitive.
unsigned primitives = 0, cut_at = 0;
void die(void *, const char *) noexcept {
	if (primitives++ == cut_at)
		::_exit(0);
}
int cut_child(const std::filesystem::path &directory, unsigned cut) {
	Agent agent(directory);
	cut_at = cut;
	l::AgentStoreTesting::install(&die, nullptr);
	agent.publish(directory, changes(R, h, ring, agent.account));
	l::AgentStoreTesting::install(nullptr, nullptr);
	return 3; // the cut lies past the publication and its collection
}
int spawn_cut(const std::string &self, const std::filesystem::path &directory, unsigned cut) {
	auto path = directory.string();
	auto argument = std::to_string(cut);
	char *arguments[]{const_cast<char *>(self.c_str()), const_cast<char *>("--cut"), path.data(), argument.data(),
					  nullptr};
	pid_t child = -1;
	require(::posix_spawn(&child, self.c_str(), nullptr, nullptr, arguments, environ) == 0);
	int status = 0;
	require(::waitpid(child, &status, 0) == child && WIFEXITED(status));
	return WEXITSTATUS(status);
}
l::paged::Digest commitment_of(std::shared_ptr<const d::ValidatedSnapshot> snapshot,
							   char generation) {
	l::GenerationId id{};
	id.fill(generation);
	l::detail::PagedSnapshotBuilder builder(std::move(snapshot), id);
	return l::paged::state_commitment(drive(builder).root());
}

// s7 durability F3: a store killed at every primitive of a catch-up's publication (and the collection
// that follows) opens, selects R or h, and from R each catch-up (the ring entries, the block, a base)
// reaches the full copy's commitment, published over whatever the cut publication left.
void after_cuts(const std::string &self, const std::filesystem::path &root, const l::detail::PagedImage &base_image,
				const l::paged::Digest &expected) {
	unsigned cut = 0, from_r = 0;
	for (;; ++cut) {
		require(cut < 1000);
		const auto directory = root / ("cut-" + std::to_string(cut));
		install(directory, base_image);
		const auto status = spawn_cut(self, directory, cut);
		require(status == 0 || status == 3);
		{
			Agent agent(directory);
			const auto selected = agent.store->selection().descriptor;
			require(selected && (selected->cursor.revision == R || selected->cursor.revision == h));
			if (selected->cursor.revision == h) {
				require(Agent::state_of(agent.recover(directory)) == expected);
			} else {
				++from_r;
				{
					auto recovered = agent.recover(directory);
					for (const auto *list : {&block, &ring}) {
						l::detail::PagedDeltaPlanBuilder builder(recovered, changes(R, h, *list, agent.account),
																 agent.domain);
						while (!builder.done()) {
							l::ValidationBudget budget;
							builder.step(budget);
						}
						require(l::paged::state_commitment(builder.take().root()) == expected);
					}
					l::detail::PagedSnapshotPlanBuilder copy(target_copy(), recovered, agent.store->directory_identity(),
															 agent.domain.factory(), agent.account,
															 agent.options.recovery_limits);
					while (!copy.done()) {
						l::ValidationBudget budget;
						copy.step(budget);
					}
					require(l::paged::state_commitment(copy.take().root()) == expected);
				}
				agent.publish(directory, changes(R, h, ring, agent.account));
				require(Agent::state_of(agent.recover(directory)) == expected);
			}
		}
		std::filesystem::remove_all(directory);
		if (status == 3)
			break;
	}
	require(cut > 10 && from_r > 0);
	std::printf("catch-up after %u cuts: %u recovered at R and caught up\n", cut + 1, from_r);
}
} // namespace

int main(int argc, char **argv) {
	if (argc == 4 && std::string_view(argv[1]) == "--cut") {
		try {
			return cut_child(argv[2], unsigned(std::stoul(argv[3])));
		} catch (const std::exception &error) {
			std::fprintf(stderr, "cut child: %s\n", error.what());
			return 1;
		}
	}
	const auto self = std::filesystem::absolute(argv[0]).string();
	const auto directory = std::filesystem::current_path() / ("agent-catch-up-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	try {
		// The agent's store at R = 10: app:a, app:b, app:c.
		l::GenerationId base_id{};
		base_id.fill('r');
		l::detail::PagedSnapshotBuilder base_builder(base_copy(), base_id);
		const auto base_image = drive(base_builder);
		install(directory, base_image);

		auto account = std::make_shared<d::StorageLedger>(64 * l::paged::segment_bytes);
		auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(account, 64);
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(16 * l::paged::segment_bytes);
		std::optional<l::RecoveredGeneration> base(l::recover_generation(directory, limits));
		auto apply = [&](d::ValidatedDelta delta, char generation) {
			l::GenerationId next{};
			next.fill(generation);
			l::detail::PagedDeltaPlanBuilder builder(*base, std::move(delta), domain);
			return l::paged::state_commitment(drive(builder).root());
		};

		const auto expected = commitment_of(full_copy(h, h, {{"app:b", {14, 21}}, {"app:c", {13, 30}}, {"app:d", {11, 4}}}), 'f');
		const auto from_ring = apply(changes(R, h, ring, account), 'x');
		const auto from_block = apply(changes(R, h, block, account), 'y');
		const auto from_base = commitment_of(full_copy(h, h, {{"app:b", {14, 21}}, {"app:c", {13, 30}}, {"app:d", {11, 4}}}),
											 'z');
		require(from_ring == expected);
		require(from_block == expected);
		require(from_base == expected);
		require(expected != l::paged::state_commitment(base_image.root()));

		// A block whose one change at a position is such a tombstone moves only the namespace's position.
		const auto tombstone_only = apply(changes(R, 11, {{11, "app:z", {}}}, account), 'w');
		require(tombstone_only ==
				commitment_of(full_copy(11, 11, {{"app:a", {3, 1}}, {"app:b", {5, 2}}, {"app:c", {7, 3}}}), 'v'));
		// The control: a tombstone for a key the store holds is a delete, not a no-op.
		const auto deleted = apply(changes(R, 11, {{11, "app:a", {}}}, account), 'u');
		require(deleted == commitment_of(full_copy(11, 11, {{"app:b", {5, 2}}, {"app:c", {7, 3}}}), 't'));
		require(deleted != tombstone_only);

		base.reset();
		domain.close().wait();
		runtime.shutdown();
		after_cuts(self, directory / "cuts", base_image, expected);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_agent_catch_up: %s\n", error.what());
		return 1;
	}
	return 0;
}
