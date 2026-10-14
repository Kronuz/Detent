#pragma once
#include "node.hh"  // first: it names the sha256 library's SHA256 before OpenSSL's function
#include "consensus/transport/peers.h"
#include "consensus/transport/profile.h"

// The authority's binding of the consensus peer transport: its Node is the host, and
// its options give the transport's identity and the profile both ends compare.
namespace detent::authority {
using AuthorityPeers = kronuz::consensus::transport::Peers<Node>;
inline kronuz::consensus::transport::PeerConfiguration peer_configuration(const NodeOptions &options) {
	return {options.configuration,
			options.consensus,
			options.context.cluster,
			options.context.group,
			options.context.epoch,
			kronuz::consensus::transport::profile_fingerprint(semantic_profile(options.application),
															 options.configuration, options.consensus)};
}
} // namespace detent::authority
