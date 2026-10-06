#pragma once
#include "hello.hh"
#include <asio/ip/address.hpp>
#include <asio/ssl.hpp>
#include <map>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <set>

namespace detent::transport {
struct TlsOptions {
	std::string certificate, private_key, certificate_authority;
};
enum class TlsRole { Peer, PublisherServer, PublisherClient };
inline void configure_tls(asio::ssl::context &context, const TlsOptions &options, TlsRole role) {
	const bool certificate_required = role != TlsRole::PublisherClient;
	if (options.certificate_authority.empty() ||
		(certificate_required && (options.certificate.empty() || options.private_key.empty())) ||
		(options.certificate.empty() != options.private_key.empty()))
		throw std::invalid_argument("incomplete TLS configuration");
	context.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
						asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 |
						asio::ssl::context::no_tlsv1_1);
	if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION) != 1)
		throw std::runtime_error("TLS minimum version");
	SSL_CTX_set_max_early_data(context.native_handle(), 0);
	SSL_CTX_set_options(context.native_handle(), SSL_OP_NO_RENEGOTIATION);
	SSL_CTX_set_max_cert_list(context.native_handle(), 64 * 1024);
	if (!options.certificate.empty()) {
		context.use_certificate_chain_file(options.certificate);
		context.use_private_key_file(options.private_key, asio::ssl::context::pem);
		if (SSL_CTX_check_private_key(context.native_handle()) != 1)
			throw std::invalid_argument("TLS key mismatch");
	}
	context.load_verify_file(options.certificate_authority);
	context.set_verify_mode(role == TlsRole::Peer
								? asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert
							: role == TlsRole::PublisherClient ? asio::ssl::verify_peer
															   : asio::ssl::verify_none);
}
inline Fingerprint certificate_fingerprint(X509 *certificate) {
	if (!certificate)
		throw std::invalid_argument("missing certificate");
	Fingerprint digest{};
	unsigned length = 0;
	if (X509_digest(certificate, EVP_sha256(), digest.data(), &length) != 1 || length != digest.size())
		throw std::runtime_error("certificate fingerprint");
	return digest;
}
inline void validate_peer_identity(asio::ssl::context &context,
								   const cluster::consensus::FixedConfiguration &configuration,
								   const std::map<cluster::consensus::NodeId, Fingerprint> &pins) {
	std::set<cluster::consensus::NodeId> voters(configuration.voters.begin(), configuration.voters.end());
	if (voters.empty() || voters.contains(0) || voters.size() != configuration.voters.size() ||
		!voters.contains(configuration.local) || pins.size() != voters.size())
		throw std::invalid_argument("invalid voter pin membership");
	std::set<Fingerprint> unique;
	for (const auto &[node, fingerprint] : pins) {
		if (!voters.contains(node) || !unique.insert(fingerprint).second)
			throw std::invalid_argument("duplicate or unknown voter pin");
	}
	const auto local = certificate_fingerprint(SSL_CTX_get0_certificate(context.native_handle()));
	if (CRYPTO_memcmp(local.data(), pins.at(configuration.local).data(), local.size()) != 0)
		throw std::invalid_argument("local voter certificate mismatch");
}
// The HTTPS client must bind the verified chain to its requested DNS name.
// Peer connections instead bind the verified leaf to a configured voter pin.
inline void configure_publisher_client(SSL *ssl, const std::string &hostname) {
	if (hostname.empty() || hostname.find('\0') != std::string::npos)
		throw std::invalid_argument("publisher TLS hostname");
	asio::error_code error;
	(void)asio::ip::make_address(hostname, error);
	if (!error) {
		if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), hostname.c_str()) != 1)
			throw std::invalid_argument("publisher TLS IP address");
	} else if (SSL_set_tlsext_host_name(ssl, hostname.c_str()) != 1 ||
			   X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), hostname.c_str(), hostname.size()) != 1)
		throw std::invalid_argument("publisher TLS hostname");
}
inline cluster::consensus::Identity fresh_connection_identity() {
	cluster::consensus::Identity result;
	do {
		if (RAND_bytes(reinterpret_cast<unsigned char *>(result.data()), result.size()) != 1)
			throw std::runtime_error("connection identity entropy");
	} while (result == cluster::consensus::Identity{});
	return result;
}
// Pin the verified leaf certificate to one configured voter. A trusted CA alone
// cannot give an arbitrary certificate holder a voter identity.
inline cluster::consensus::NodeId
authenticated_voter(SSL *ssl, const std::map<cluster::consensus::NodeId, Fingerprint> &configured) {
	if (!ssl || !(SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) || SSL_get_verify_result(ssl) != X509_V_OK)
		throw std::invalid_argument("unverified peer certificate");
	std::unique_ptr<X509, decltype(&X509_free)> certificate(SSL_get1_peer_certificate(ssl), X509_free);
	if (!certificate)
		throw std::invalid_argument("missing peer certificate");
	const auto digest = certificate_fingerprint(certificate.get());
	cluster::consensus::NodeId found = 0;
	for (const auto &[node, pin] : configured) {
		if (CRYPTO_memcmp(pin.data(), digest.data(), digest.size()) == 0) {
			if (!node || found)
				throw std::invalid_argument("ambiguous voter certificate");
			found = node;
		}
	}
	if (!found)
		throw std::invalid_argument("unknown voter certificate");
	return found;
}
} // namespace detent::transport
