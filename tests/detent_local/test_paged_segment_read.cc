#include "paged_disk_fixture.hh"
#include "paged_segment_read.hh"
#include <completion/native_completion.h>
namespace c = kronuz::io::completion;
void drive(l::detail::PagedSegmentRead &reader) {
	for (unsigned turns = 0; !reader.done(); ++turns) {
		require(turns < 100000);
		if (reader.needs_work()) {
			l::ValidationBudget budget{1, 1};
			reader.step(budget);
		} else {
			reader.submitted();
			require(
				reader.complete(kronuz::journal::detail::execute_primitive(reader.io(), reader.request())));
		}
	}
}
int main() {
	const auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-segment-read-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	l::detail::PagedSnapshotBuilder builder(source, storage, generation);
	while (!builder.done()) {
		l::ValidationBudget budget;
		builder.step(budget);
	}
	auto image = builder.take();
	install(directory, image, storage);
	require(image.segments().size() == 1);
	const auto &segment = image.segments().front();
	const l::detail::SegmentNames names(segment.ownership.segment);
	const auto owner_path = directory / names.owner_name();
	const auto pack_path = directory / names.payload_name();
	const auto original = std::span<const char>(segment.bytes->data(), segment.bytes->size());
	auto account = std::make_shared<d::StorageLedger>(32 * l::paged::segment_bytes);
	auto baseline = std::make_shared<d::StorageLedger>(l::paged::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(account, 64);
	const auto initial = account->used();
	{
		auto session = l::make_owned_storage_session(domain, directory, std::make_shared<int>(1), true);
		for (unsigned variant = 0; variant < 9; ++variant) {
			auto expected = segment.ownership;
			const std::string large(65536 + 17, 'x');
			const auto payload = variant == 8 ? std::span<const char>(large) : original;
			if (variant == 8) {
				expected.length = payload.size();
				expected.checksum = l::paged::physical_commitment(payload);
			}
			write(owner_path, l::encode_segment_ownership(expected));
			write(pack_path, payload);
			if (variant == 1) {
				std::string corrupt(original.begin(), original.end());
				corrupt.back() ^= 1;
				write(pack_path, corrupt);
			} else if (variant == 2)
				write(pack_path, original.first(original.size() - 1));
			l::detail::PagedSegmentRead reader(domain.factory(), session, account, storage,
											   segment.ownership.segment, ::getuid() + (variant == 3));
			bool fired = false;
			unsigned pack_reads = 0;
			const auto injected =
				std::make_exception_ptr(std::runtime_error("segment read error during cancellation"));
			for (unsigned turns = 0; !reader.done(); ++turns) {
				require(turns < 100000);
				if (reader.needs_work()) {
					l::ValidationBudget zero{0, 0};
					reader.step(zero);
					require(reader.needs_work());
					if (variant == 7 && !fired) {
						auto pressure = account->admit(account->limit() - account->used());
						l::ValidationBudget budget{1, 1};
						reader.step(budget);
						fired = true;
					} else {
						l::ValidationBudget budget{variant == 8 ? 8192u : 1u, 1};
						reader.step(budget);
					}
					continue;
				}
				const auto &request = reader.request();
				const bool pack_read =
					request.kind == c::Kind::Read && request.source == names.payload_name();
				if (pack_read) {
					++pack_reads;
					require(request.destination_bytes.size() <= 8192);
				}
				reader.submitted();
				c::Result result;
				if (variant == 6 && pack_read && !fired) {
					reader.close();
					result.token = request.token;
					result.error = injected;
					fired = true;
				} else {
					result = kronuz::journal::detail::execute_primitive(reader.io(), request);
					if (pack_read && !fired && variant == 4) {
						auto ownership = segment.ownership;
						ownership.checksum[0] ^= 1;
						write(owner_path, l::encode_segment_ownership(ownership));
						fired = true;
					} else if (pack_read && !fired && variant == 5) {
						const auto replacement = directory / "replacement.pack";
						write(replacement, original);
						std::filesystem::rename(replacement, pack_path);
						fired = true;
					}
				}
				c::Result stale;
				stale.token = result.token;
				++stale.token.step;
				require(!reader.complete(std::move(stale)) && reader.in_flight());
				require(reader.complete(std::move(result)));
			}
			if (!variant || variant == 8) {
				require(reader.ready() && !reader.error() && reader.ownership() == expected);
				require(reader.bytes()->view() == std::string_view(payload.data(), payload.size()) &&
						reader.bytes()->sealed());
				if (variant == 8)
					require(pack_reads == 9);
				require(reader.identity().inode &&
						reader.identity().device == session->directory_identity().device);
				require(reader.owner_file()->inspect().inode &&
						reader.payload_file()->inspect().inode == reader.identity().inode);
			} else {
				require(!reader.ready() && reader.error());
				if (variant >= 4)
					require(fired);
				if (variant == 6)
					require(reader.error() == injected);
			}
			reader.close();
			drive(reader);
			require(reader.done() && !reader.ready() && !reader.in_flight());
		}
		session->close();
	}
	domain.close().wait();
	require(account->used() == initial);
}
