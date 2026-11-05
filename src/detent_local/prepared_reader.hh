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
// One selected generation's checked records, with its lease descriptor holding a shared flock.
struct AttachedGeneration {
	int lease = -1;
	Descriptor descriptor;
	StorageIdentity storage{};
	paged::Root root;
	ReaderMetadata metadata;
	ViewLimits limits; // narrowed to the generation's own counts
	AttachedGeneration() = default;
	AttachedGeneration(AttachedGeneration &&other) noexcept;
	AttachedGeneration &operator=(AttachedGeneration &&) = delete;
	~AttachedGeneration();
};
// Qualification hook after opening a record and before its ownership check, on this thread.
struct PreparedReaderTesting {
	static void install(void (*)(std::string_view, void *), void *) noexcept;
};
class PreparedReader {
  public:
	static GenerationSelection selection(const std::filesystem::path &, uid_t publisher);
	// The selected descriptor, read from a trusted store directory; GenerationUnavailable when absent.
	static Descriptor selected(int directory, uid_t publisher);
	// Attaches the selected generation through a trusted store directory without opening it again.
	static AttachedGeneration attach(int directory, uid_t publisher, ViewLimits,
	                                 std::optional<distribution::Cursor> expected = {});
    static ReaderMetadata describe(const RecoveredGeneration &);
    static bool metadata_ready(const std::filesystem::path &, const RecoveredGeneration &, uid_t publisher);
	static PreparedGeneration open(const std::filesystem::path &, uid_t publisher, ViewLimits,
	                               std::optional<distribution::Cursor> expected = {});
};
} // namespace detent::local::detail
