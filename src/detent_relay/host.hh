#pragma once
#include "../detent_cli/daemon.hh"

namespace detent::relay {
int run_host(const std::filesystem::path &configuration, const detent_cli::Ready &);
}
