#pragma once
#include "detent/read_view.hh"
#include "reader.hh"

namespace detent::local {
struct ClientSourceOptions {
	std::filesystem::path directory;
	ViewLimits limits;
	std::optional<distribution::Cursor> identity;
	std::chrono::milliseconds poll_interval{200};
	bool auto_refresh = true;
    // Explicit publisher trust enables synchronous, checked-path attachment.
    std::optional<std::uint32_t> prepared_publisher;
};
// Each Client receives its own source with immutable defaults/CLI overlays.
// Automatic refresh runs on one configuration worker. Manual refresh belongs
// on the host's worker; all scoped acquisitions perform no filesystem work.
class LocalReadSource final : public ReadSource {
  public:
	LocalReadSource(ClientSourceOptions, std::shared_ptr<const Snapshot>,
					std::shared_ptr<const OverrideTable>);
	~LocalReadSource() override;
	ClientReadView read_view() const noexcept override;
	ReadSourceStatus status() const noexcept override;
	bool refresh();

  private:
	struct Impl;
	distribution::StorageOwner<Impl> impl_;
};
ReadSourceFactory make_local_read_source(ClientSourceOptions);
ReadSourceFactory make_managed_read_source(std::filesystem::path, std::uint32_t publisher,
                                         ViewLimits = ViewLimits::prepared(),
                                         std::optional<distribution::Cursor> identity = {});
} // namespace detent::local
