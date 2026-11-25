#pragma once
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "snapshot_fixture.hh"
#include "trusted_records.hh"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
namespace l = detent::local;
inline void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), bytes.size());
	out.close();
	require(bool(out));
	require(::chmod(path.c_str(), 0600) == 0);
}
using trusted_records::install;
