#pragma once
#include "hello.hh"
#include "state.hh"
#include <sha256.h>

namespace detent::transport {
inline Fingerprint profile_fingerprint(const detent::authority::Limits &application,
									   const kronuz::consensus::FixedConfiguration &configuration,
									   const kronuz::consensus::Limits &consensus) {
	using kronuz::journal::put64;
	auto voters = configuration.voters;
	std::sort(voters.begin(), voters.end());
	if (voters.empty() || voters.size() > consensus.voters || voters.front() == 0 ||
		std::adjacent_find(voters.begin(), voters.end()) != voters.end() ||
		!std::binary_search(voters.begin(), voters.end(), configuration.local))
		throw std::invalid_argument("invalid profile membership");
	std::string bytes("DTPROF01", 8);
	bytes += detent::authority::semantic_profile(application);
	for (auto id : {configuration.cluster, configuration.configuration})
		bytes.append(id.data(), id.size());
	put64(bytes, voters.size());
	for (auto voter : voters)
		put64(bytes, voter);
	for (auto limit : {consensus.voters, consensus.command_bytes, consensus.log_bytes, consensus.log_entries,
					   consensus.control_entries, consensus.rpc_bytes, consensus.rpc_entries, consensus.reads,
					   consensus.uncommitted_entries})
		put64(bytes, limit);
	SHA256 hash;
	hash.add(bytes.data(), bytes.size());
	Fingerprint result{};
	hash.getHash(result.data());
	return result;
}
} // namespace detent::transport
