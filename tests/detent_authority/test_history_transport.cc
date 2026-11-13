// Peer fetch over the real transport (S5.3 A1): three voters on TLS peers, each serving
// its history directory through `history_reader` and fetching through `PeerHistory`. A
// voter whose transport starts only after the others enabled positions, wrote and took
// checkpoints is installed from a snapshot with an empty history directory: it fetches
// every file from its peers over the fetch lane, and its files and answers are the
// same bytes as theirs. Voter two serves every file rotten, so voter three passes over it
// whenever it asks it first (F1), and asks it first for some files. Journals and history directories live in memory (journal's
// MemoryIO), so the run waits on the transport, not a device; crash safety is the power-cut tests'.
#include "peer_history.hh"
#include "consensus/transport/tls.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <array>
#include <filesystem>
#include <functional>
#include <iostream>
#include <journal/memory_io.h>
#include <journal/mutation.h>
#include <openssl/pem.h>

using namespace kronuz::consensus::transport;
using namespace detent::authority;
using namespace kronuz::consensus;
using namespace std::chrono_literals;
namespace h = detent::authority::history;

namespace {
Fingerprint pin(const std::filesystem::path &path) {
	std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(path.c_str(), "r"), BIO_free);
	assert(input);
	std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr),
													 X509_free);
	return certificate_fingerprint(cert.get());
}
namespace mem = kronuz::journal::testing;
std::map<std::string, std::string> files(mem::Disk &disk) {
	h::Directory history(std::make_shared<mem::MemoryIO>(disk), {});
	std::map<std::string, std::string> out;
	for (const auto &name : history.names())
		out[name] = history.read(name);
	return out;
}
} // namespace

int main(int argc, char **argv) {
	assert(argc == 2);
	std::filesystem::path certificates(argv[1]);
	std::array<mem::Disk, 3> journals, histories;
	asio::io_context io;
	std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> reservations;
	std::vector<asio::ip::tcp::endpoint> endpoints;
	for (unsigned i = 0; i != 3; ++i) {
		auto acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
		endpoints.push_back(acceptor->local_endpoint());
		reservations.push_back(std::move(acceptor));
	}
	LayoutPolicy policy;
	policy.seal_transactions = 4;
	policy.period = 4;
	policy.carry_bytes = 64;
	policy.carry_count = 2;
	policy.alpha_num = 1;
	policy.alpha_den = 2;
	const std::vector<std::string> names{"one", "two", "three"};
	std::map<NodeId, Fingerprint> pins;
	for (unsigned i = 0; i != 3; ++i)
		pins.emplace(i + 1, pin(certificates / (names[i] + ".pem")));
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<NodeOptions> configurations;
	std::vector<std::shared_ptr<PeerHistory>> fetchers;
	for (unsigned i = 0; i != 3; ++i) {
		NodeOptions options;
		options.timing = {100, 500, 1000, 2000, 60000};
		options.context.cluster[0] = 'c';
		options.context.group[0] = 'g';
		options.context.epoch[0] = 'e';
		options.configuration = {options.context.cluster, options.context.group, i + 1, {1, 2, 3}};
		options.application.layout = policy;
		auto node = std::make_shared<Node>(std::make_unique<mem::MemoryIO>(journals[i]), options);
		std::vector<NodeId> others;
		for (NodeId voter = 1; voter <= 3; ++voter)
			if (voter != i + 1)
				others.push_back(voter);
		fetchers.push_back(std::make_shared<PeerHistory>(io.get_executor(), others, 4, 50ms));
		node->attach_history(std::make_shared<mem::MemoryIO>(histories[i]), fetchers.back()->fetch());
		Identity identity{};
		identity[0] = static_cast<char>(i + 1);
		node->create(identity);
		for (unsigned turn = 0; !node->worker().ready() && turn != 100; ++turn) {
			node->service({0, 1100 + i * 300});
			if (auto job = node->worker().take_io_operation())
				kronuz::journal::drive_synchronously(*job);
			node->take_network_actions();
		}
		assert(node->worker().ready());
		node->service({0, 1100 + i * 300});
		node->start();
		configurations.push_back(options);
		nodes.push_back(std::move(node));
	}
	reservations.clear();
	// Each voter's reader is the production one, timed: its run time lands on the owner executor.
	double slowest_read_ms = 0;
	std::size_t largest = 0, rotten = 0;
	// Voter one's reader fails on this name and voter two holds none: a fetch of it ends.
	const auto failing = h::seal_name(std::uint64_t{1} << 40);
	std::vector<std::shared_ptr<AuthorityPeers>> peers(3);
	auto start_peers = [&](unsigned i) {
		PeerOptions options;
		options.tls = {(certificates / (names[i] + ".pem")).string(), (certificates / (names[i] + ".key")).string(),
					   (certificates / "ca.pem").string()};
		options.pins = pins;
		options.listen = endpoints[i];
		options.deadline = 1000ms;
		for (unsigned j = 0; j != 3; ++j)
			if (i != j)
				options.addresses.emplace(j + 1, PeerAddress{endpoints[j].address().to_string(), endpoints[j].port()});
		options.reader = [read = history_reader(std::make_shared<mem::MemoryIO>(histories[i]), options.fetch_bytes),
						  &slowest_read_ms, &largest, &rotten, &failing, i](std::string_view name) {
			if (i == 0 && name == failing)
				throw std::runtime_error("unreadable history file");
			const auto started = std::chrono::steady_clock::now();
			auto bytes = read(name);
			if (i == 1 && bytes && !bytes->empty()) {
				(*bytes)[bytes->size() / 2] ^= 1;
				++rotten;
			}
			slowest_read_ms = std::max(
				slowest_read_ms,
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
			largest = std::max(largest, bytes ? bytes->size() : 0);
			return bytes;
		};
		peers[i] = std::make_shared<AuthorityPeers>(io.get_executor(), nodes[i], peer_configuration(configurations[i]),
													options);
		fetchers[i]->attach(peers[i]);
		peers[i]->start();
	};
	std::vector<std::vector<Completion>> completed(3);
	const auto origin = std::chrono::steady_clock::now();
	std::optional<unsigned> leader;
	auto pump = [&] {
		const auto tick = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - origin).count());
		for (unsigned i = 0; i != 3; ++i) {
			if (peers[i])
				peers[i]->admit();
			nodes[i]->service({tick, 1100 + i * 300});
			if (auto job = nodes[i]->worker().take_io_operation())
				kronuz::journal::drive_synchronously(*job);
			if (auto job = nodes[i]->take_history_operation())
				kronuz::journal::drive_synchronously(*job);
			assert(!nodes[i]->worker().fenced());
			for (auto &completion : nodes[i]->take_completions())
				completed[i].push_back(std::move(completion));
			for (auto &action : nodes[i]->take_network_actions())
				if (auto send = std::get_if<Send>(&action); send && peers[i])
					peers[i]->send(std::move(*send));
			if (peers[i])
				peers[i]->pump();
			if (nodes[i]->worker().role() == Role::Leader)
				leader = i;
		}
		io.run_for(1ms);
		if (io.stopped())
			io.restart();
	};
	auto until = [&](const std::function<bool()> &predicate, const char *what) {
		for (unsigned turn = 0; turn != 20000 && !predicate(); ++turn)
			pump();
		if (!predicate()) {
			std::cerr << "timed out waiting for " << what << "\n";
			std::abort();
		}
	};
	std::uint64_t request = 0;
	auto submit = [&](auto propose) {
		const auto id = ++request;
		std::optional<unsigned> at;
		until([&] {
			if (!at && leader && propose(*nodes[*leader], id) == SubmitResult::Accepted)
				at = *leader;
			return at.has_value();
		}, "a leader to accept");
		auto &done = completed[*at];
		until([&] { return std::any_of(done.begin(), done.end(), [&](auto &c) { return c.request == id; }); },
			  "a completion");
		auto found = std::find_if(done.begin(), done.end(), [&](auto &c) { return c.request == id; });
		auto result = found->results.front();
		done.erase(found);
		return result;
	};
	Context context = configurations[0].context;
	std::uint64_t sequence = 0;
	auto write = [&](unsigned count, unsigned salt) {
		const std::vector<std::string> spaces{"a", "b", "team/app"};
		for (unsigned k = 0; k != count; ++k) {
			Command command;
			command.context = context;
			command.producer = "writer";
			command.generation = 1;
			command.sequence = ++sequence;
			command.name_space = spaces[(k + salt) % spaces.size()];
			const auto key = command.name_space + ":k" + std::to_string((k * 7 + salt) % 9);
			command.mutations.push_back({key, detent::KnobDef{int(k + salt)}, {}, {}});
			auto result = submit([&](Node &node, std::uint64_t id) { return node.propose(id, {command}); });
			assert(result.status == Status::Applied || result.status == Status::NoChange);
		}
	};

	// Voters one and two form the group; three has no transport yet.
	start_peers(0);
	start_peers(1);
	until([&] { return leader.has_value(); }, "a leader");
	Command open;
	open.context = context;
	open.kind = Kind::Open;
	open.producer = "writer";
	open.nonce[0] = 'n';
	submit([&](Node &node, std::uint64_t id) { return node.propose(id, {open}); });
	write(6, 0);
	AdministrativeCommand enable{context, AdministrativeKind::EnablePositions, {}, {}, {}, {}};
	enable.nonce[0] = 's';
	assert(submit([&](Node &node, std::uint64_t id) { return node.propose_administrative(id, enable); }).status ==
		   Status::Applied);
	write(30, 1);
	// Both voters checkpoint at their durable seal, so the leader's log no longer reaches three.
	for (unsigned i = 0; i != 2; ++i) {
		until([&] { return nodes[i]->state().applied_index() == nodes[*leader]->state().applied_index(); },
			  "voters one and two to apply");
		until([&] {
			if (!nodes[i]->checkpoint_busy())
				nodes[i]->checkpoint();
			return !nodes[i]->checkpoint_busy() && nodes[i]->durable_seal() &&
				   nodes[i]->worker().base_index() == nodes[i]->durable_seal()->applied_index();
		}, "a checkpoint at the durable seal");
	}
	assert(files(histories[2]).empty() && nodes[2]->state().applied_index() < nodes[0]->worker().base_index());
	const auto snapshot = nodes[*leader]->worker().base_index();

	// Voter three joins: a snapshot install with an empty history directory.
	start_peers(2);
	until([&] {
		return nodes[2]->state().applied_index() >= snapshot && nodes[2]->history()->ready() &&
			   nodes[2]->durable_seal() && nodes[2]->durable_seal()->applied_index() == nodes[2]->sealed()->applied_index();
	}, "voter three to install");
	assert(nodes[2]->worker().base_index() >= snapshot && nodes[2]->state().positions_enabled());
	write(8, 2);
	const auto head = nodes[*leader]->state().applied_index();
	until([&] {
		for (const auto &node : nodes)
			if (node->state().applied_index() != head || node->history()->queued() ||
				node->durable_seal()->applied_index() != node->sealed()->applied_index())
				return false;
		return true;
	}, "every voter to settle");

	const auto fetched = peers[2]->stats().fetched;
	const auto served = peers[0]->stats().served + peers[1]->stats().served;
	assert(fetched > 0 && served >= fetched && rotten > 0);
	const auto reference = files(histories[0]);
	assert(reference.contains("layout"));
	for (unsigned i = 1; i != 3; ++i)
		assert(files(histories[i]) == reference);
	// The installed voter answers the same bytes as its peers, file for file.
	std::size_t compared = 0;
	for (const std::string space : {"a", "b", "team/app"}) {
		const auto *entry = nodes[0]->state().namespace_entry(space);
		assert(entry && entry->live);
		const auto *n0 = nodes[0]->history()->layout().table().find(space);
		const auto *n2 = nodes[2]->history()->layout().table().find(space);
		assert(n0 && n2 && n0->incarnation == entry->incarnation && n2->incarnation == entry->incarnation);
		h::Directory left(std::make_shared<mem::MemoryIO>(histories[0]), nodes[0]->state().limits());
		h::Directory right(std::make_shared<mem::MemoryIO>(histories[2]), nodes[2]->state().limits());
		for (Revision r = n0->base.last; r <= entry->position; ++r) {
			auto a = nodes[0]->history()->layout().answer(space, entry->incarnation, r, entry->position, left);
			auto b = nodes[2]->history()->layout().answer(space, entry->incarnation, r, entry->position, right);
			assert(a.tier == b.tier && a.through == b.through && a.names == b.names && a.parts == b.parts);
			++compared;
		}
	}
	// A file no voter can serve ends the fetch with every voter's answer, not a retry forever.
	std::optional<h::Reply> unserved;
	fetchers[2]->fetch()(failing, [](std::string_view) { return true; }, [&](h::Reply reply) {
		unserved = std::move(reply);
	});
	until([&] { return unserved.has_value(); }, "a fetch no voter can serve to end");
	assert(!unserved->bytes && unserved->why.find("voter 1 ") != std::string::npos &&
		   unserved->why.find("voter 2 none") != std::string::npos);
	for (auto &fetcher : fetchers)
		fetcher->stop();
	for (auto &runtime : peers)
		runtime->stop();
	until([&] {
		for (auto &runtime : peers)
			if (!runtime->drained())
				return false;
		return true;
	}, "the transports to drain");
	std::cout << "voter three installed at index " << snapshot << " with an empty history directory: " << fetched
			  << " files fetched over the fetch lane, " << reference.size() << " history files identical on three voters, "
			  << compared << " answers equal, " << rotten << " rotten copies passed over; an unservable file ended ("
			  << unserved->why << "); slowest reader run " << slowest_read_ms << " ms for files up to " << largest
			  << " bytes\n";
}
