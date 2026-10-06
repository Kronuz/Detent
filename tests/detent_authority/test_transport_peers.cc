#include "transport/peers.hh"
#include <cassert>
#include <filesystem>
#include <future>
#include <iostream>
#include <journal/posix.h>
#include <openssl/pem.h>
#include <unistd.h>

using namespace detent::transport;
using namespace detent::authority;
using namespace cluster::consensus;
using namespace std::chrono_literals;

Fingerprint pin(const std::filesystem::path &path) {
	std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(path.c_str(), "r"), BIO_free);
	assert(input);
	std::unique_ptr<X509, decltype(&X509_free)> cert(
		PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
	return certificate_fingerprint(cert.get());
}
int main(int argc, char **argv) {
	assert(argc == 2);
	std::filesystem::path certificates(argv[1]);
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("tls-peer-group-" + std::to_string(getpid()));
	std::filesystem::create_directories(directory);
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::filesystem::remove_all(path); }
	} cleanup{directory};
	asio::io_context io;
	std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> reservations;
	std::vector<asio::ip::tcp::endpoint> endpoints;
	for (unsigned i = 0; i != 3; ++i) {
		auto acceptor = std::make_unique<asio::ip::tcp::acceptor>(
			io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
		endpoints.push_back(acceptor->local_endpoint());
		reservations.push_back(std::move(acceptor));
	}
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<Peers>> peers;
	std::vector<NodeOptions> configurations;
	std::vector<PeerOptions> peer_options;
	const std::vector<std::string> names{"one", "two", "three"};
	std::map<NodeId, Fingerprint> pins;
	for (unsigned i = 0; i != 3; ++i)
		pins.emplace(i + 1, pin(certificates / (names[i] + ".pem")));
	for (unsigned i = 0; i != 3; ++i) {
		NodeOptions options;
		options.timing = {100, 500, 1000, 2000, 60000};
		options.context.cluster[0] = 'c';
		options.context.group[0] = 'g';
		options.context.epoch[0] = 'e';
		options.configuration = {options.context.cluster, options.context.group, i + 1, {1, 2, 3}};
		auto path = directory / names[i];
		std::filesystem::create_directory(path);
		auto node = std::make_shared<Node>(std::make_unique<kronuz::journal::PosixIO>(path), options);
		Identity identity{};
		identity[0] = static_cast<char>(i + 1);
		node->create(identity);
		for (unsigned turn = 0; !node->worker().ready() && turn != 100; ++turn) {
			node->service({0, 1100 + i * 300});
			node->take_network_actions();
		}
		assert(node->worker().ready());
		node->service({0, 1100 + i * 300});
		node->start();
		configurations.push_back(options);
		nodes.push_back(std::move(node));
	}
	reservations.clear();
	for (unsigned i = 0; i != 3; ++i) {
		PeerOptions options;
		options.tls = {(certificates / (names[i] + ".pem")).string(),
					   (certificates / (names[i] + ".key")).string(), (certificates / "ca.pem").string()};
		options.pins = pins;
		options.listen = endpoints[i];
		options.deadline = 1000ms;
		for (unsigned j = 0; j != 3; ++j)
			if (i != j)
				options.addresses.emplace(j + 1, endpoints[j]);
		auto runtime = std::make_shared<Peers>(io.get_executor(), nodes[i], configurations[i], options);
		peer_options.push_back(options);
		peers.push_back(runtime);
		runtime->start();
	}
	auto exercise = [&]() -> asio::awaitable<void> {
		const auto origin = std::chrono::steady_clock::now();
		asio::steady_timer timer(io);
		std::optional<unsigned> leader;
		bool submitted = false, committed = false;
		auto pump = [&] {
			const auto tick = std::chrono::duration_cast<std::chrono::milliseconds>(
								  std::chrono::steady_clock::now() - origin)
								  .count();
			for (unsigned i = 0; i != 3; ++i) {
				assert(1100 + i * 300 <= configurations[i].timing.election_max);
				peers[i]->admit();
				nodes[i]->service({static_cast<std::uint64_t>(tick), 1100 + i * 300});
				for (auto &action : nodes[i]->take_network_actions())
					if (auto send = std::get_if<Send>(&action))
						peers[i]->send(std::move(*send));
				peers[i]->pump();
				if (nodes[i]->worker().role() == Role::Leader)
					leader = i;
			}
		};
		for (unsigned turn = 0; turn != 10000 && !committed; ++turn) {
			pump();
			if (leader && !submitted) {
				Command command;
				command.context = configurations[*leader].context;
				command.kind = Kind::Open;
				command.producer = "tls-publisher";
				command.nonce[0] = 'n';
				submitted = nodes[*leader]->propose(1, {command}) == SubmitResult::Accepted;
			}
			committed = submitted;
			for (auto &node : nodes)
				committed = committed && node->state().producer("tls-publisher") != nullptr;
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		assert(committed && leader);
		for (auto &runtime : peers)
			assert(runtime->stats().admitted >= 4);
		// Replacing a follower's transport must retire both old lane identities
		// and allow new authenticated connections without restarting its store.
		const auto follower = (*leader + 1) % 3;
		peers[follower]->stop();
		while (!peers[follower]->drained()) {
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		for (unsigned sequence = 1; sequence <= 4; ++sequence) {
			Command command;
			command.context = configurations[*leader].context;
			command.producer = "tls-publisher";
			command.generation = 1;
			command.sequence = sequence;
			command.name_space = "ns";
			command.mutations.push_back({"ns:key", detent::IntDef{sequence}, {}, {}});
			bool proposed = false;
			for (unsigned turn = 0; turn != 5000 && nodes[*leader]->state().revision() < sequence; ++turn) {
				pump();
				if (!proposed)
					proposed = nodes[*leader]->propose(sequence + 1, {command}) == SubmitResult::Accepted;
				timer.expires_after(1ms);
				co_await timer.async_wait(asio::use_awaitable);
			}
			assert(nodes[*leader]->state().revision() == sequence);
		}
		for (unsigned voter = 0; voter != 3; ++voter) {
			if (voter == follower)
				continue;
			for (unsigned turn = 0; turn != 5000 && (nodes[voter]->state().revision() != 4 ||
													 nodes[voter]->worker().applied_index() !=
														 nodes[voter]->state().applied_index());
				 ++turn) {
				pump();
				timer.expires_after(1ms);
				co_await timer.async_wait(asio::use_awaitable);
			}
			assert(nodes[voter]->checkpoint());
			for (unsigned turn = 0; turn != 5000 && nodes[voter]->checkpoint_busy(); ++turn) {
				pump();
				timer.expires_after(1ms);
				co_await timer.async_wait(asio::use_awaitable);
			}
			assert(!nodes[voter]->checkpoint_busy());
		}

		assert(nodes[follower]->state().revision() == 0);
		peers[follower] = std::make_shared<Peers>(io.get_executor(), nodes[follower],
												  configurations[follower], peer_options[follower]);
		peers[follower]->start();
		bool reconnected = false;
		for (unsigned turn = 0; turn != 10000 && !reconnected; ++turn) {
			pump();
			reconnected = peers[follower]->stats().admitted >= 4 &&
						  nodes[follower]->state().revision() == 4 &&
						  nodes[follower]->worker().base_index() > 0;
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		assert(reconnected);
		for (auto &runtime : peers)
			runtime->stop();
		for (unsigned turn = 0; turn != 3000; ++turn) {
			bool drained = true;
			for (auto &runtime : peers)
				drained = drained && runtime->drained();
			if (drained)
				co_return;
			timer.expires_after(1ms);
			co_await timer.async_wait(asio::use_awaitable);
		}
		assert(false && "peer shutdown did not reap original operations");
	};
	auto completion = asio::co_spawn(io, exercise(), asio::use_future);
	io.run();
	completion.get();
	std::cout << "three TLS voters: authenticated lanes, durable replication and reaped shutdown passed\n";
}
