#include "peers.hh"
#include "consensus/transport/tls.h"
#include <cassert>
#include <filesystem>
#include <future>
#include <iostream>
#include <journal/mutation.h>
#include <completion/asio_completion.h>
#include <completion/native_completion.h>
#include <completion/posix.h>
#include <openssl/pem.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace kronuz::consensus::transport;
using namespace detent::authority;
using namespace kronuz::consensus;
using namespace std::chrono_literals;

Fingerprint pin(const std::filesystem::path &path) {
	std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(path.c_str(), "r"), BIO_free);
	assert(input);
	std::unique_ptr<X509, decltype(&X509_free)> cert(
		PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
	return certificate_fingerprint(cert.get());
}
int main(int argc, char **argv) {
	assert(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--completion-storage"));
	const bool completion_storage = argc == 3;
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
	std::vector<std::shared_ptr<AuthorityPeers>> peers;
	std::vector<NodeOptions> configurations;
	std::vector<PeerOptions> peer_options;
	using Queue = kronuz::journal::NativeCompletionQueue;
	using Driver = kronuz::journal::AsioCompletionDriver<Queue>;
	std::vector<std::shared_ptr<Queue>> queues;
	std::vector<std::shared_ptr<Driver>> drivers;
	std::array<bool, 3> active{};
	std::exception_ptr storage_error;
	const std::vector<std::string> names{"one", "two", "three"};
	std::map<NodeId, Fingerprint> pins;
	for (unsigned i = 0; i != 3; ++i)
		pins.emplace(i + 1, pin(certificates / (names[i] + ".pem")));
	for (unsigned i = 0; i != 3; ++i) {
		NodeOptions options;
		options.scheduling.async_storage = completion_storage;
		options.timing = {100, 500, 1000, 2000, 60000};
		options.context.cluster[0] = 'c';
		options.context.group[0] = 'g';
		options.context.epoch[0] = 'e';
		options.configuration = {options.context.cluster, options.context.group, i + 1, {1, 2, 3}};
		auto path = directory / names[i];
		std::filesystem::create_directory(path);
		assert(::chmod(path.c_str(), 0700) == 0);
		auto node = std::make_shared<Node>(std::make_unique<kronuz::journal::PosixIO>(path), options);
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
		if (completion_storage) {
#ifdef __linux__
			auto queue = std::make_shared<Queue>(kronuz::journal::LinuxCompletionPolicy::RequireNative);
#else
			auto queue = std::make_shared<Queue>();
#endif
			drivers.push_back(std::make_shared<Driver>(io.get_executor(), queue));
			queues.push_back(std::move(queue));
		}
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
		auto runtime = std::make_shared<AuthorityPeers>(io.get_executor(), nodes[i], peer_configuration(configurations[i]), options);
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
				if (completion_storage) {
					if (auto operation = nodes[i]->worker().take_io_operation()) {
						assert(!active[i]);
						active[i] = true;
						asio::co_spawn(io, drivers[i]->run(operation),
									   [&, i, owner = nodes[i], operation](std::exception_ptr error) {
										   if (error) {
											   storage_error = error;
											   owner->worker().storage_operation_failed(
												   operation, "native peer fixture IO failed");
										   }
										   active[i] = false;
									   });
					}
				}
				assert(!storage_error && !nodes[i]->worker().fenced());
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
		peers[follower] = std::make_shared<AuthorityPeers>(io.get_executor(), nodes[follower],
														   peer_configuration(configurations[follower]), peer_options[follower]);
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
			pump();
			bool drained = true;
			for (auto &runtime : peers)
				drained = drained && runtime->drained();
			for (unsigned i = 0; i != 3; ++i)
				drained = drained && !active[i] && nodes[i]->channel().drained();
			if (drained) {
				if (completion_storage)
					for (auto &queue : queues) {
						assert(!queue->busy());
						const auto stats = queue->stats();
						assert(stats.native_completed > 0 &&
							   stats.native_submitted == stats.native_completed);
						std::cout << "native=" << stats.native_completed
								  << " fallback=" << stats.fallback_completed << '\n';
					}
				co_return;
			}
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
