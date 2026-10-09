#pragma once
#include <asio/ip/address.hpp>
#include <asio/ssl.hpp>
#include <openssl/x509.h>

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
} // namespace detent::transport
