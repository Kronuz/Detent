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
	static ReaderMetadata describe(const RecoveredGeneration &);
	static PreparedGeneration open(const std::filesystem::path &, uid_t publisher, ViewLimits,
	                               std::optional<distribution::Cursor> expected = {});
};
} // namespace detent::local::detail
