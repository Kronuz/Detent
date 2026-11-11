// Voter hosts in the authority configuration: syntax checks, host-and-port duplicates, and names
// passed through as written, with no lookup (D60).
#include "service.hh"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
namespace {
const std::string fingerprint(64, 'a');
std::filesystem::path config;

std::string voter(unsigned id, const std::string &host, unsigned port) {
	return "{\"id\":" + std::to_string(id) + ",\"host\":\"" + host + "\",\"port\":" + std::to_string(port) +
		   ",\"certificate_sha256\":\"" + fingerprint + "\"}";
}
// Loads a three-voter configuration for node 1 with these hosts; returns the refusal, or empty.
std::string refusal(const std::string &one, const std::string &two, const std::string &three,
					ServiceOptions *loaded = nullptr, unsigned port_two = 7002) {
	{
		std::ofstream file(config);
		file << "{\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
				"\"epoch\":\"65000000000000000000000000000000\",\"directory\":\"state\",\"node\":1,\"voters\":["
			 << voter(1, one, 7001) << "," << voter(2, two, port_two) << "," << voter(3, three, 7003)
			 << "],\"peer_tls\":{\"certificate\":\"c\",\"key\":\"k\",\"ca\":\"a\"},"
				"\"publisher_tls\":{\"certificate\":\"c\",\"key\":\"k\",\"ca\":\"a\"},\"credentials\":[{"
				"\"token\":\"0123456789abcdef0123456789abcdef\",\"producer\":\"agent\",\"namespaces\":[\"ns\"]}]}";
	}
	assert(::chmod(config.c_str(), 0600) == 0);
	try {
		auto options = load_service_options(config);
		if (loaded)
			*loaded = std::move(options);
		return {};
	} catch (const std::invalid_argument &error) {
		return error.what();
	}
}
std::string host_refusal(const std::string &host) { return refusal("127.0.0.1", host, "127.0.0.3"); }
} // namespace

int main() {
	config = std::filesystem::current_path() / ".scratch" / ("voter-hosts-" + std::to_string(::getpid()) + ".json");
	std::filesystem::create_directories(config.parent_path());

	// Names and literals load, and the transport receives them as written.
	ServiceOptions loaded;
	assert(refusal("127.0.0.1", "voter-2.example.invalid", "[::1]", &loaded).find("voter host") == 0);
	assert(refusal("127.0.0.1", "voter-2.example.invalid", "::1", &loaded).empty());
	assert(loaded.peers->addresses.at(2).host == "voter-2.example.invalid" && loaded.peers->addresses.at(2).port == 7002);
	assert(loaded.peers->addresses.at(3).host == "::1");
	assert(loaded.peers->listen.address().to_string() == "127.0.0.1" && loaded.peers->listen.port() == 7001);
	assert(refusal("Node-1.Example.INVALID", "a", "a1-b.c", &loaded).empty());
	// A named local voter binds the IPv4 wildcard; the name is not resolved to bind.
	assert(loaded.peers->listen.address().is_unspecified() && loaded.peers->listen.port() == 7001);
	assert(refusal("localhost", "localhost", "localhost", &loaded, 7002).empty());
	assert(refusal(std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." +
					   std::string(61, 'd'),
				   "b", "c")
			   .empty()); // 253 bytes

	// Invalid hosts are refused by name of the rule.
	assert(host_refusal("") == "voter host must be 1 to 253 bytes");
	assert(host_refusal(std::string(254, 'a')) == "voter host must be 1 to 253 bytes");
	assert(host_refusal(std::string(63 * 4 + 2, 'a')) == "voter host must be 1 to 253 bytes");
	assert(host_refusal(std::string(2000, 'a')) == "invalid string");
	assert(host_refusal(std::string(64, 'a') + ".invalid") == "voter host name label");
	for (const char *bad : {"-a", "a-", "a..b", ".a", "a.", "a.-b", "a-.b"})
		assert(host_refusal(bad) == "voter host name label");
	for (const char *bad : {"a_b", "a b", "a:80", "a/b", "a@b", "\\u00e9", "a%b"})
		assert(host_refusal(bad) == "voter host name characters" || host_refusal(bad) == "voter host name label");
	for (const char *bad : {"0.0.0.0", "::", "224.0.0.1", "ff02::1"})
		assert(host_refusal(bad) == "voter host IP literal");
	for (const char *bad : {"1.2.3", "300.1.1.1", "host.123"})
		assert(host_refusal(bad) == "voter host name must not end in a numeric label");

	// Duplicates are by host and port as written.
	assert(refusal("a.invalid", "a.invalid", "c.invalid", nullptr, 7001) == "duplicate voter host and port");
	assert(refusal("127.0.0.1", "127.0.0.1", "127.0.0.1", nullptr, 7001) == "duplicate voter host and port");
	assert(refusal("a.invalid", "a.invalid", "a.invalid", nullptr, 7002).empty()); // same host, other ports

	std::filesystem::remove(config);
	std::cout << "voter hosts passed\n";
}
