// Dense distribution DD3, the agent's half: a cold agent opens with "pages":true, verifies the
// server's page transfer through DD1's receiver and publishes it with no packer run (no build hash);
// its generation is the one a records build of the same state writes, pack for pack. A transfer
// whose root is wrong is refused, the next open asks that namespace as records, and the records
// build publishes the same generation. A clustered change then comes as a delta transfer over the
// agent's generation, and publishes with no build either.
//   test_pages_coordinator CERTIFICATES [KEYS]   KEYS definitions in the namespace (100,000)
#include "coordinator_agent_fixture.hh"
#include "detent_delivery/hash_stats.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include "detent_local/paged_definition.hh"
#include "paged_limits.hh"
#include "paged_navigation.hh"
#include "paged_overlay_assembler.hh"
#include "paged_page_access.hh"
#include "paged_snapshot_builder.hh"
#include "paged_trie_overlay.hh"
#include "page_window.hh"
#include "paged_transfer.hh"
#include <cstring>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace t = detent::delivery;
namespace l = detent::local;
namespace p = l::paged;
namespace x = l::detail;
using namespace namespace_server;
using coordinator_agent::Agent;
using coordinator_agent::matches;

namespace {
// `keys` definitions in one transaction, then three small ones: a block and a ring above the cut.
std::vector<Transaction> corpus(const std::string &name, std::size_t keys) {
	std::vector<Transaction> out(1);
	char key[16];
	for (std::size_t i = 0; i < keys; ++i) {
		std::snprintf(key, sizeof key, ":k%07zu", i);
		out[0].push_back({name + key, def(std::int64_t(i), int(i))});
	}
	for (int i = 0; i < 3; ++i)
		out.push_back({{name + ":k" + std::to_string(i), def(1000 + i, 0)}});
	return out;
}
// The server's canonical copy of `served` at its head: the full build of its state, named as an
// agent names a full copy over no base, and the transfer of its pages in the build's files' order,
// which is seal order.
struct Canonical {
	l::Descriptor descriptor;
	p::Root root;
	p::Reference tree;
	std::vector<x::BuiltSegment> segments;
	std::map<l::SegmentId, std::string> packs;
	Transfer transfer;
};
Canonical canonical(const Served &served, const l::ViewLimits &view) {
	const auto history = served.history();
	const auto to = history.head();
	auto account = std::make_shared<d::StorageLedger>(std::size_t(2) << 30);
	d::Limits limits;
	limits.storage_account = account;
	limits.storage_bytes = account->limit();
	d::FrameInput input(served.name, d::FrameKind::FullCopy, 0, to, d::Profile{}, limits);
	input.piece(d::PieceKind::FullCopy, 0, to.position);
	for (const auto &[key, held] : history.state_at(to.position))
		input.record(key, held.revision, &*held.value);
	const auto bounds = x::paged_limits(view, p::root_bytes);
	x::PagedSnapshotBuilder builder(input.snapshot(), l::next_generation(std::nullopt, to, d::Profile{}, true), bounds);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	Canonical out;
	out.descriptor = image.descriptor();
	out.root = image.root();
	out.tree = image.root().tree;
	out.segments.assign(image.segments().begin(), image.segments().end());
	std::vector<p::Reference> order;
	for (const auto &segment : image.segments()) {
		const std::span<const char> bytes(segment.bytes->data(), segment.bytes->size());
		out.packs.emplace(segment.ownership.segment, std::string(bytes.data(), bytes.size()));
		for (std::size_t at = 0; at < bytes.size();) {
			const auto length = std::uint32_t(l::read_integer(bytes, at + 8, 4));
			order.push_back({segment.ownership.segment, std::uint32_t(at), length});
			at = (at + length + 7) & ~std::size_t(7);
		}
	}
	out.transfer.bytes = x::make_transfer(
		order,
		[&out](const p::Reference &r) {
			return std::span<const char>(out.packs.at(r.segment)).subspan(r.offset, r.length);
		},
		bounds);
	out.transfer.answer = {image.root().tree.digest, 0, to, {}};
	return out;
}
x::TransferPages reader(const Canonical &copy) {
	return [&copy](const p::Reference &r) {
		return std::span<const char>(copy.packs.at(r.segment)).subspan(r.offset, r.length);
	};
}
// The delta transfer from `base` to `target`: the target's pages the base lacks, in seal order (D7.3).
Transfer delta(const Canonical &base, const Canonical &target, const l::ViewLimits &view) {
	const auto bounds = x::paged_limits(view, p::root_bytes);
	const auto diff = detent::relay::merkle_diff(target.tree, reader(target), base.tree, reader(base), bounds);
	Transfer out;
	out.bytes = x::make_transfer(diff.pages, reader(target), bounds);
	out.answer = {target.tree.digest, base.descriptor.cursor.position, target.descriptor.cursor, {}};
	return out;
}
// A delta from `base` to its tree with one definition of namespace "bad" added (dense-pages D7):
// honest pages and root, under `to`, whose new pages hold a leaf of another namespace.
Transfer foreign_delta(const Canonical &base, const d::NamespaceCursor &to, const l::ViewLimits &view) {
	const auto bounds = x::paged_limits(view, p::root_bytes);
	auto account = std::make_shared<d::StorageLedger>(std::size_t(2) << 30);
	std::vector<x::SegmentInput> inputs;
	for (const auto &segment : base.segments)
		inputs.push_back({segment.ownership, {segment.bytes->data(), segment.bytes->size()}});
	std::sort(inputs.begin(), inputs.end(), [](const auto &a, const auto &b) {
		return std::memcmp(a.ownership.segment.data(), b.ownership.segment.data(), a.ownership.segment.size()) < 0;
	});
	std::vector<x::SegmentReachability> live(inputs.size());
	for (std::vector<p::Reference> stack{base.tree}; !stack.empty();) {
		const auto r = stack.back();
		stack.pop_back();
		const auto i = x::find_segment(inputs, r.segment);
		const auto bytes = x::checked_page_bytes(inputs[i], r);
		++live[i].pages;
		live[i].bytes += r.length;
		const auto header = p::decode_page_header(bytes, bounds);
		for (std::uint16_t e = 0; e < header.externals; ++e)
			stack.push_back(p::decode_external(bytes, e, bounds));
	}
	detent::KnobDef value{5};
	std::vector<char> body(p::definition_size(value));
	p::encode_definition(body, value, 1);
	x::TrieOverlay overlay(account, std::make_shared<int>(0), inputs, base.root, bounds);
	overlay.change(std::string_view("Kbad\0k1\0", 8), std::span<const char>(body));
	while (!overlay.operation_done()) {
		l::ValidationBudget budget;
		overlay.step(budget);
	}
	overlay.seal();
	l::GenerationId id;
	id.fill('f');
	x::OverlayAssembler assembler(account, overlay, inputs, live, base.root, {id, 0}, to, bounds);
	while (!assembler.done()) {
		l::ValidationBudget budget;
		assembler.step(budget);
	}
	auto image = assembler.take();
	std::map<l::SegmentId, std::span<const char>> pages;
	for (const auto &input : inputs)
		pages.emplace(input.ownership.segment, input.bytes);
	for (const auto &segment : image.segments())
		pages.emplace(segment.ownership.segment, std::span<const char>(segment.bytes->data(), segment.bytes->size()));
	const x::TransferPages read = [&pages](const p::Reference &r) { return pages.at(r.segment).subspan(r.offset, r.length); };
	const auto diff = detent::relay::merkle_diff(image.root().tree, read, base.tree, reader(base), bounds);
	Transfer out;
	out.bytes = x::make_transfer(diff.pages, read, bounds);
	out.answer = {image.root().tree.digest, base.descriptor.cursor.position, to, {}};
	return out;
}
void install(Server &server, const std::string &name, Transfer transfer) {
	std::lock_guard lock(server.mutex);
	auto &held = server.transfers[name] = std::move(transfer);
	held.answer.bytes = held.bytes;
}
std::string hex(std::span<const char> bytes) {
	std::string out;
	for (unsigned char byte : bytes) {
		out += "0123456789abcdef"[byte >> 4];
		out += "0123456789abcdef"[byte & 15];
	}
	return out;
}
// The agent's selected generation of `name` is the canonical one, and its packs are its files.
void same_generation(const t::CoordinatorStatus &state, const std::filesystem::path &root, const std::string &name,
					 const Canonical &expected) {
	const auto *selected = state.selected.find(name);
	check(selected && *selected == expected.descriptor);
	for (const auto &[segment, bytes] : expected.packs) {
		std::ifstream file(root / ("segment-" + hex(segment) + ".pack"), std::ios::binary);
		const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		check(written == bytes);
	}
}
bool at(const t::CoordinatorStatus &state, const std::string &name, const d::NamespaceCursor &head) {
	const auto found = std::find_if(state.namespaces.begin(), state.namespaces.end(),
									[&](const auto &each) { return each.name == name; });
	return state.ready && found != state.namespaces.end() && found->cursor == head;
}
} // namespace

int main(int argc, char **argv) {
	const auto root = std::filesystem::current_path() / ".scratch" / ("pages-coordinator-" + std::to_string(::getpid()));
	try {
		check(argc == 2 || argc == 3);
		const std::size_t keys = argc == 3 ? std::stoul(argv[2]) : 100000;
		const std::filesystem::path certificates(argv[1]);
		auto server_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*server_tls,
								{(certificates / "one.pem").string(), (certificates / "one.key").string(),
								 (certificates / "ca.pem").string()},
								reactor::tls::Role::Server);
		auto client_tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
		reactor::tls::configure(*client_tls, {"", "", (certificates / "ca.pem").string()}, reactor::tls::Role::Client);
		Server server(server_tls, {"big", "bad"});
		server.served = {{"bad", id('b'), 1, corpus("bad", 2000)}, {"big", id('g'), 2, corpus("big", keys)}};

		std::filesystem::create_directories(root);
		std::filesystem::permissions(root, std::filesystem::perms::owner_all);
		auto account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);
		d::RetirementRuntime runtime(account, 1);
		auto domain = runtime.domain(account, 256);
		auto queue = kronuz::io::completion::make_native_queue(
			kronuz::io::completion::AllocationContext(d::make_storage_resource(account)));
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(std::size_t(1) << 30);

		const auto big = canonical(server.find("big"), limits);
		const auto bad = canonical(server.find("bad"), limits);
		install(server, "big", big.transfer);
		auto wrong = bad.transfer;
		wrong.answer.root[0] ^= 1; // a root the pages do not hash to
		install(server, "bad", std::move(wrong));
		const auto big_head = server.find("big").history().head(), bad_head = server.find("bad").history().head();

		t::CoordinatorOptions options;
		options.namespaces = {"big"};
		options.directory = root / "big";
		options.view_limits = limits;
		options.retry_interval = 5ms;
		options.reconnect_maximum = 50ms;
		options.reconnect_seed = 7;
		options.cluster.fill('\x01');
		options.source.endpoint = server.acceptor.local_endpoint();
		options.source.hostname = "localhost";
		options.source.token = "0123456789abcdef0123456789abcdef";
		options.source.tls_context = client_tls;
		options.source.timeout = 30s;
		options.source.compression = server.compressed;
		options.source.limits.storage_account = account;
		options.source.limits.storage_bytes = account->limit();
		for (const auto &directory : {root / "big", root / "bad"}) {
			std::filesystem::create_directories(directory);
			std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		}
		{
			// A cold agent boots from the transfer: it verifies and writes it, and builds nothing. Its
			// first answer is cut halfway; the next resumes from the whole chunks it holds (D8).
			{
				std::lock_guard lock(server.mutex);
				server.cut = big.transfer.bytes.size() / 2;
			}
			const auto before = t::hash_stats();
			const auto started = std::chrono::steady_clock::now();
			Agent agent(root / "big", l::StoreOpenMode::FreshEmpty, options, account, domain, queue);
			const auto state = agent.until([&](const auto &s) { return at(s, "big", big_head); }, "big from pages", 120s);
			const auto elapsed = std::chrono::steady_clock::now() - started;
			const auto spent = t::hash_stats() - before;
			check(state.transfers.received == 1 && state.transfers.refused == 0 && state.failures == 1);
			check(state.transfers.bytes == big.transfer.bytes.size());
			{
				std::lock_guard lock(server.mutex);
				check(server.offsets.size() == 2 && server.offsets[0] == 0 && server.offsets[1] >= d::transfer_chunk_bytes &&
					  server.offsets[1] < big.transfer.bytes.size() && server.offsets[1] % d::transfer_chunk_bytes == 0);
				std::cout << "resumed: the cut answer's next sent the transfer from " << server.offsets[1] << " of "
						  << big.transfer.bytes.size() << " bytes\n";
			}
			check(spent.built.page_calls == 0 && spent.built.pack_calls == 0);
			same_generation(state, root / "big", "big", big);
			matches(server, root / "big", limits, "big");
			{
				std::lock_guard lock(server.mutex);
				check(!server.records_asked.empty() &&
					  std::all_of(server.records_asked.begin(), server.records_asked.end(),
								  [](const auto &names) { return names.empty(); }));
			}
			// A clustered change of a tenth of the keys comes as a delta transfer over the agent's base,
			// installed with the change so the agent never sees the change without it.
			Served changed = server.find("big");
			Transaction change;
			for (std::size_t i = keys / 3; i < keys / 3 + keys / 10; ++i) {
				char key[16];
				std::snprintf(key, sizeof key, ":k%07zu", i);
				change.push_back({"big" + std::string(key), def(std::int64_t(i) + 7, 0)});
			}
			changed.transactions.push_back(change);
			const auto big2 = canonical(changed, limits);
			auto moved = delta(big, big2, limits);
			const auto delta_bytes = moved.bytes.size();
			const auto before_delta = t::hash_stats();
			{
				std::lock_guard lock(server.mutex);
				server.find("big").transactions.push_back(std::move(change));
				auto &held = server.transfers["big"] = std::move(moved);
				held.answer.bytes = held.bytes;
			}
			const auto next = agent.until([&](const auto &s) { return at(s, "big", big2.descriptor.cursor); },
										  "big's delta from pages", 120s);
			const auto delta_spent = t::hash_stats() - before_delta;
			if (next.transfers.received != 2 || next.transfers.refused || next.failures != state.failures)
				throw std::runtime_error("delta: received " + std::to_string(next.transfers.received) + ", refused " +
										 std::to_string(next.transfers.refused) + ", failures " +
										 std::to_string(next.failures) + ": " + coordinator_agent::message(next.error));
			check(next.transfers.bytes == big.transfer.bytes.size() + delta_bytes && delta_bytes < big2.transfer.bytes.size());
			check(delta_spent.built.page_calls == 0 && delta_spent.built.pack_calls == 0);
			const auto *selected = next.selected.find("big");
			check(selected && selected->cursor == big2.descriptor.cursor && selected->generation != big2.descriptor.generation);
			matches(server, root / "big", limits, "big");
			std::cout << "delta pages: " << keys / 10 << " keys changed, " << delta_bytes << " transfer bytes (full copy "
					  << big2.transfer.bytes.size() << "), built hashes " << delta_spent.built.page_calls << "+"
					  << delta_spent.built.pack_calls << ", checked page hashes " << delta_spent.checked.page_calls << "\n";
			// A delta whose root is wrong is refused, and the next open asks big as records (D9).
			Served again = server.find("big");
			Transaction more{{"big:k0000001", def(77, 0)}};
			again.transactions.push_back(more);
			auto wrong_delta = delta(big2, canonical(again, limits), limits);
			wrong_delta.answer.root[0] ^= 1;
			{
				std::lock_guard lock(server.mutex);
				server.find("big").transactions.push_back(std::move(more));
				auto &held = server.transfers["big"] = std::move(wrong_delta);
				held.answer.bytes = held.bytes;
				server.records_asked.clear();
			}
			const auto third = agent.until([&](const auto &s) { return at(s, "big", again.history().head()); },
										   "big's refused delta as records", 120s);
			check(third.transfers.received == 2 && third.transfers.refused == 1);
			matches(server, root / "big", limits, "big");
			{
				std::lock_guard lock(server.mutex);
				check(std::find(server.records_asked.begin(), server.records_asked.end(),
								std::vector<std::string>{"big"}) != server.records_asked.end());
			}
			// F1: a delta whose new pages hold another namespace's leaf, honestly rooted, is refused, and
			// the next open asks big as records.
			Transaction last{{"big:k0000002", def(78, 0)}};
			Served moved_on = again;
			moved_on.transactions.push_back(last);
			auto foreign = foreign_delta(canonical(again, limits), moved_on.history().head(), limits);
			{
				std::lock_guard lock(server.mutex);
				server.find("big").transactions.push_back(std::move(last));
				auto &held = server.transfers["big"] = std::move(foreign);
				held.answer.bytes = held.bytes;
				server.records_asked.clear();
				server.open_session = false; // the session that asked big as records expires; the next asks pages
			}
			const auto fourth = agent.until([&](const auto &s) { return at(s, "big", moved_on.history().head()); },
											"big's foreign delta as records", 120s);
			check(fourth.transfers.received == 2 && fourth.transfers.refused == 2);
			matches(server, root / "big", limits, "big");
			{
				std::lock_guard lock(server.mutex);
				check(std::find(server.records_asked.begin(), server.records_asked.end(),
								std::vector<std::string>{"big"}) != server.records_asked.end());
			}
			std::cout << "pages: " << keys << " keys, " << big.transfer.bytes.size() << " transfer bytes, "
					  << big.packs.size() << " packs, built hashes " << spent.built.page_calls << "+"
					  << spent.built.pack_calls << ", checked page hashes " << spent.checked.page_calls << " ("
					  << spent.checked.page_bytes << " B), pack hashes " << spent.checked.pack_calls << " ("
					  << spent.checked.pack_bytes << " B), "
					  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms\n";
		}
		{
			// A wrong root is refused; the next open asks bad as records, whose build is the same generation.
			{
				std::lock_guard lock(server.mutex);
				server.records_asked.clear();
			}
			options.namespaces = {"bad"};
			options.directory = root / "bad";
			Agent agent(root / "bad", l::StoreOpenMode::FreshEmpty, options, account, domain, queue);
			auto state = agent.until([&](const auto &s) { return at(s, "bad", bad_head); }, "bad from records", 120s);
			check(state.transfers.refused == 1 && state.transfers.received == 0 && state.failures >= 1);
			same_generation(state, root / "bad", "bad", bad);
			matches(server, root / "bad", limits, "bad");
			std::lock_guard lock(server.mutex);
			check(server.records_asked.front().empty());
			check(std::find(server.records_asked.begin(), server.records_asked.end(), std::vector<std::string>{"bad"}) !=
				  server.records_asked.end());
		}
		{
			// F1, the review's probe: big is served bad's honest, correctly rooted full copy under big's
			// cursor. The agent refuses it, and big comes as records with big's own reads.
			const auto head = server.find("big").history().head();
			Transfer foreign = bad.transfer;
			foreign.answer.to = head;
			install(server, "big", std::move(foreign));
			{
				std::lock_guard lock(server.mutex);
				server.records_asked.clear();
			}
			options.namespaces = {"big"};
			options.directory = root / "foreign";
			std::filesystem::create_directories(root / "foreign");
			std::filesystem::permissions(root / "foreign", std::filesystem::perms::owner_all);
			Agent agent(root / "foreign", l::StoreOpenMode::FreshEmpty, options, account, domain, queue);
			const auto state = agent.until([&](const auto &s) { return at(s, "big", head); }, "foreign copy as records", 120s);
			check(state.transfers.refused == 1 && state.transfers.received == 0);
			matches(server, root / "foreign", limits, "big");
			{
				std::lock_guard lock(server.mutex);
				check(std::find(server.records_asked.begin(), server.records_asked.end(),
								std::vector<std::string>{"big"}) != server.records_asked.end());
			}
			// D1: a records frame names its sender's root. One that the build reaches publishes; one it
			// does not is refused and the namespace is fetched again as a full copy.
			const auto rooted = [&](std::int64_t value) {
				std::lock_guard lock(server.mutex);
				auto &served = server.find("big");
				served.transactions.push_back({{"big:k0000003", def(value, 0)}});
				const auto copy = canonical(served, limits);
				const auto commitment = p::state_commitment(copy.root);
				d::RootDigest digest;
				std::copy(commitment.begin(), commitment.end(), digest.begin());
				return std::pair{served.history().head(), digest};
			};
			auto honest = rooted(91);
			{
				std::lock_guard lock(server.mutex);
				server.roots["big"] = honest;
			}
			auto rooted_state = agent.until([&](const auto &s) { return at(s, "big", honest.first); }, "rooted records");
			check(rooted_state.transfers.diverged == 0);
			auto wrong = rooted(92);
			wrong.second[0] ^= 1;
			{
				std::lock_guard lock(server.mutex);
				server.roots["big"] = wrong;
			}
			agent.until([](const auto &s) { return s.transfers.diverged > 0 && s.repair.requested > 0; }, "diverged records");
			{
				std::lock_guard lock(server.mutex);
				server.roots.clear();
			}
			rooted_state = agent.until([&](const auto &s) { return at(s, "big", wrong.first) && s.repair.completed > 0; },
									   "diverged records repaired");
			matches(server, root / "foreign", limits, "big");
			// D139: a repair while the source's newest build is older than the agent's position. The repair
			// names that position; an older source that ignores it sends a copy below it, which the agent
			// skips; a source that honors it answers nothing until its head reaches it.
			const auto floor = wrong.first;
			bool below = false;
			const auto never_below = [&](const t::CoordinatorStatus &s) {
				for (const auto &each : s.namespaces)
					if (each.name == "big" && each.cursor && each.cursor->position < floor.position)
						below = true;
			};
			std::vector<Transaction> kept;
			{
				std::lock_guard lock(server.mutex);
				auto &served = server.find("big");
				kept = served.transactions;
				served.transactions.resize(served.transactions.size() - 2);
				server.ignore_floor = true;
				server.open_session = false;
			}
			const auto stale = canonical(server.find("big"), limits);
			check(stale.descriptor.cursor.position + 2 == floor.position);
			install(server, "big", stale.transfer);
			const auto completed = rooted_state.repair.completed;
			agent.repair("big");
			auto repairing = agent.until(
				[&](const auto &s) {
					never_below(s);
					return s.repair.below > 0;
				},
				"a copy below the floor skipped");
			check(at(repairing, "big", floor) && repairing.repair.pending && repairing.repair.completed == completed);
			const auto idle = [&] {
				std::lock_guard lock(server.mutex);
				server.ignore_floor = false;
				return server.idle.load();
			}();
			repairing = agent.until(
				[&](const auto &s) {
					never_below(s);
					return server.idle > idle + 1;
				},
				"no answer below the floor");
			check(at(repairing, "big", floor) && repairing.repair.pending);
			{
				std::lock_guard lock(server.mutex);
				check(!server.repairs.empty() && server.repairs.back() == floor);
				auto &served = server.find("big");
				served.transactions = std::move(kept);
				served.transactions.push_back({{"big:k0000004", def(93, 0)}});
			}
			const auto healed = canonical(server.find("big"), limits);
			install(server, "big", healed.transfer);
			repairing = agent.until(
				[&](const auto &s) {
					never_below(s);
					return at(s, "big", healed.descriptor.cursor) && s.repair.completed > completed;
				},
				"the repair at or above the floor", 120s);
			check(!below && healed.descriptor.cursor.position > floor.position);
			same_generation(repairing, root / "foreign", "big", healed);
		}
		check(!server.failure);
		queue.reset();
		domain.close().wait();
		runtime.shutdown();
		std::filesystem::remove_all(root);
		std::cout << "a cold agent boots from pages with no build, and a refused transfer falls back to records\n";
	} catch (const std::exception &error) {
		std::cerr << "test_pages_coordinator: " << error.what() << '\n';
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
		return 1;
	}
	return 0;
}
