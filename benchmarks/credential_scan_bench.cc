#include <array>
#include <chrono>
#include <iostream>
#include <sha256.h>
#include <string>
#include <sys/resource.h>
#include <vector>
using Digest = std::array<unsigned char, 32>;
Digest digest(const std::string &token) {
	SHA256 hash;
	hash.add(token.data(), token.size());
	Digest value{};
	hash.getHash(value.data());
	return value;
}
double cpu() {
	rusage value{};
	getrusage(RUSAGE_SELF, &value);
	return value.ru_utime.tv_sec + value.ru_utime.tv_usec / 1e6 + value.ru_stime.tv_sec +
		   value.ru_stime.tv_usec / 1e6;
}
int main() {
	std::vector<std::string> tokens;
	std::vector<Digest> cached;
	for (unsigned i = 0; i < 769; ++i) {
		tokens.push_back(std::string(32, 'x') + std::to_string(i));
		cached.push_back(digest(tokens.back()));
	}
	std::cout << "pair,mode,requests,credentials,cpu_seconds,matches\n";
	for (unsigned pair = 1; pair <= 3; ++pair)
		for (bool precomputed : {false, true}) {
			auto before = cpu();
			unsigned matches = 0;
			for (unsigned request = 0; request < 5000; ++request) {
				auto wanted = digest(tokens[request % tokens.size()]);
				for (std::size_t i = 0; i < tokens.size(); ++i) {
					auto stored = precomputed ? cached[i] : digest(tokens[i]);
					unsigned difference = 0;
					for (unsigned j = 0; j < 32; ++j)
						difference |= stored[j] ^ wanted[j];
					if (!difference)
						++matches;
				}
			}
			std::cout << pair << ',' << (precomputed ? "cached" : "rehash") << ",5000,769," << cpu() - before
					  << ',' << matches << '\n';
		}
}
