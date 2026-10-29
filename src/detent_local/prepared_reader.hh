#pragma once
#include "reader_metadata.hh"
#include <sys/types.h>

namespace detent::local::detail {
struct PreparedGeneration {
	ReadView view;
	Descriptor descriptor;
	DirectoryIdentity directory_identity;
	paged::Digest logical;
};
class PreparedReader {
  public:
	static GenerationSelection selection(const std::filesystem::path &, uid_t publisher);
    static ReaderMetadata describe(const RecoveredGeneration &);
    static bool metadata_ready(const std::filesystem::path &, const RecoveredGeneration &, uid_t publisher,
                               std::optional<ReaderMetadataEncoding> required = {});
	static PreparedGeneration open(const std::filesystem::path &, uid_t publisher, ViewLimits,
	                               std::optional<distribution::Cursor> expected = {});
};
} // namespace detent::local::detail
