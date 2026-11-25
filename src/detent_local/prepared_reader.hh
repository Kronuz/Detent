#pragma once
#include "reader_metadata.hh"
#include <sys/types.h>

namespace detent::local::detail {
struct Selected;
struct PreparedGeneration {
	ReadView view;
	Descriptor descriptor;
	DirectoryIdentity directory_identity;
	paged::Digest state_commitment;
};
// One selected generation's checked records, with its lease descriptor holding a shared flock.
struct AttachedGeneration {
	int lease = -1;
	Descriptor descriptor;
	paged::Root root;
	ReaderMetadata metadata;
	ViewLimits limits; // narrowed to the generation's own counts
	AttachedGeneration() = default;
	AttachedGeneration(AttachedGeneration &&other) noexcept;
	AttachedGeneration &operator=(AttachedGeneration &&) = delete;
	~AttachedGeneration();
};
class PreparedReader {
  public:
	static GenerationSelection selection(const std::filesystem::path &, uid_t publisher, std::string_view name = {});
	// The table entry `name`, read from a trusted store directory; GenerationUnavailable when absent.
	static Selected selected(int directory, uid_t publisher, std::string_view name = {});
	// Attaches the entry's selected generation through a trusted store directory without opening it
	// again; `read` is the caller's entry for the first attempt, and a retry reads `selected` again.
	static AttachedGeneration attach(int directory, uid_t publisher, ViewLimits,
	                                 std::optional<distribution::NamespaceCursor> expected = {},
	                                 const Selected *read = nullptr, std::string_view name = {});
    static ReaderMetadata describe(const RecoveredGeneration &);
    static bool metadata_ready(const std::filesystem::path &, const RecoveredGeneration &, uid_t publisher);
	static PreparedGeneration open(const std::filesystem::path &, uid_t publisher, ViewLimits,
	                               std::optional<distribution::NamespaceCursor> expected = {}, std::string_view name = {});
};
} // namespace detent::local::detail
