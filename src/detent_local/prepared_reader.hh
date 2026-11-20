#pragma once
#include "reader_metadata.hh"
#include <sys/types.h>

namespace detent::local::detail {
struct PreparedGeneration {
	ReadView view;
	Descriptor descriptor;
	DirectoryIdentity directory_identity;
	paged::Digest state_commitment;
};
class PreparedReader {
  public:
	static GenerationSelection selection(const std::filesystem::path &, uid_t publisher, std::string_view name = {});
    static ReaderMetadata describe(const RecoveredGeneration &);
    static bool metadata_ready(const std::filesystem::path &, const RecoveredGeneration &, uid_t publisher);
	static PreparedGeneration open(const std::filesystem::path &, uid_t publisher, ViewLimits,
	                               std::optional<distribution::Cursor> expected = {}, std::string_view name = {});
};
} // namespace detent::local::detail
