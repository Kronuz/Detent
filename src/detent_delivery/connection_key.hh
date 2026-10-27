#pragma once
#include "detent_delivery/client.hh"
namespace detent::delivery {
http::client::TlsKey connection_key(const ClientOptions &);
}
