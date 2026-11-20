// Dense distribution DD3, the agent's half: a cold agent opens with "pages":true, verifies the
// server's page transfer through DD1's receiver and publishes it with no packer run (no build hash);
// its generation is the one a records build of the same state writes, pack for pack. A transfer
// whose root is wrong is refused, the next open asks that namespace as records, and the records
// build publishes the same generation.
//   test_pages_coordinator CERTIFICATES [KEYS]   KEYS definitions in the namespace (100,000)
#include "coordinator_agent_fixture.hh"
#include "detent_delivery/hash_stats.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_local/derived_ids.hh"
#include "detent_local/layout.hh"
#include "paged_limits.hh"
#include "paged_snapshot_builder.hh"
#include "paged_transfer.hh"
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
			// A cold agent boots from the transfer: it verifies and writes it, and builds nothing.
			const auto before = t::hash_stats();
			const auto started = std::chrono::steady_clock::now();
			Agent agent(root / "big", l::StoreOpenMode::FreshEmpty, options, account, domain, queue);
			const auto state = agent.until([&](const auto &s) { return at(s, "big", big_head); }, "big from pages", 120s);
			const auto elapsed = std::chrono::steady_clock::now() - started;
			const auto spent = t::hash_stats() - before;
			check(state.transfers.received == 1 && state.transfers.refused == 0 && !state.failures);
			check(state.transfers.bytes == big.transfer.bytes.size());
			check(spent.built.page_calls == 0 && spent.built.pack_calls == 0);
			same_generation(state, root / "big", "big", big);
			matches(server, root / "big", limits, "big");
			std::lock_guard lock(server.mutex);
			check(!server.records_asked.empty() &&
				  std::all_of(server.records_asked.begin(), server.records_asked.end(),
							  [](const auto &names) { return names.empty(); }));
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
