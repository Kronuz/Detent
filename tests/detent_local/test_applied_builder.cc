#include "applied_delta_fixture.hh"
#include "delta_index.hh"
#include "paged_snapshot_builder.hh"
#include "pinned_source.hh"
#include "segment_names.hh"
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary);
	stream.write(bytes.data(), bytes.size());
	stream.close();
	require(bool(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
void complete(l::ImageBuilder &builder) {
	std::size_t turns = 0;
	while (!builder.done()) {
		l::ValidationBudget budget{1, 1};
		builder.step(budget);
		require(++turns < 5000000);
	}
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("applied-builder-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId id;
		id.fill('a');
		l::ImageBuilder original(source, id);
		complete(original);
		const auto name = l::generation_name(id);
		write(directory / (name + ".image"), {original.bytes()->data(), original.bytes()->size()});
		write(directory / (name + ".lease"), {});
		write(directory / "selected", l::encode_descriptor(original.descriptor()));
		for (bool progress : {false, true})
			for (unsigned tx : {1u, 2u, 17u}) {
				auto input = delta(progress, tx);
				{
					d::PagedStorage arena(*input->storage_account());
					l::detail::DeltaIndex index(arena, *input);
					for (const auto &transaction : input->transactions()) {
						for (auto offset = transaction.mutations.count; offset; --offset)
							index.append(transaction.mutations.start + offset - 1, transaction.revision);
					}
					index.seal();
					l::ValidationBudget none{0, 1};
					index.step(none);
					require(!index.done());
					unsigned turns = 0;
					while (!index.done()) {
						l::ValidationBudget b{1, 1};
						index.step(b);
						require(++turns < 1000000 && b.bytes <= 1 && b.rows <= 1);
					}
					require(index.size() == (progress ? 0 : 2));
					if (!progress) {
						require(input->mutation(index[0].ordinal).key == "app:count");
						require(input->mutation(index[1].ordinal).key == "app:flag");
						require(index[0].ordinal == 2 * (tx - 1) && index[1].ordinal == 2 * (tx - 1) + 1);
						require(index[0].revision == 42 + tx && index[1].revision == 42 + tx);
					}
				}
				auto next = id;
				next.fill('b');
				l::ImageBuilder builder(input, l::recover_generation(directory), next);
				complete(builder);
				require(builder.base_descriptor() == original.descriptor());
				require(builder.base_directory_identity() ==
						l::recover_generation(directory).directory_identity());
				auto selected = builder.descriptor();
				l::ImageBuilder oracle(expected(progress, tx), next);
				complete(oracle);
				require(oracle.descriptor() == selected);
				require(std::equal(oracle.bytes()->data(), oracle.bytes()->data() + oracle.bytes()->size(),
								   builder.bytes()->data()));
				const auto next_name = l::generation_name(next);
				write(directory / (next_name + ".image"), {builder.bytes()->data(), builder.bytes()->size()});
				write(directory / (next_name + ".lease"), {});
				write(directory / "selected", l::encode_descriptor(selected));
				{
					l::detail::PinnedImageSource result(l::recover_generation(directory));
					require(result.descriptor().cursor == input->target());
					require(result.name_space(0).revision == (progress ? 42 : 42 + tx));
					require(result.record_count() == (!progress && tx == 1 ? 1 : 2));
					if (progress) {
						require(result.record(0).revision == 41 && result.record(1).revision == 42);
						require(
							std::get<l::detail::SourceBoolean>(result.record(1).definition).override_count ==
							1);
					} else {
						auto flag = result.record(result.record_count() - 1);
						require(flag.key == "app:flag" && flag.revision == 42 + tx);
						require(std::get<l::detail::SourceBoolean>(flag.definition).default_value ==
								bool((tx - 1) % 2));
						if (tx > 1)
							require(std::get<detent::IntDef>(result.record(0).definition).default_value ==
									tx - 1);
					}
				}
				bool rejected = false;
				try {
					l::ImageBuilder wrong(input, l::recover_generation(directory), id);
				} catch (const std::invalid_argument &) {
					rejected = true;
				}
				require(rejected);
				write(directory / "selected", l::encode_descriptor(original.descriptor()));
			}

		{
			const auto paged_directory = directory / "paged";
			std::filesystem::create_directories(paged_directory);
			std::filesystem::permissions(paged_directory, std::filesystem::perms::owner_all);
			l::StorageIdentity storage{};
			storage.fill('s');
			l::detail::PagedSnapshotBuilder encoder(source, storage, id);
			while (!encoder.done()) {
				l::ValidationBudget budget{1, 1};
				encoder.step(budget);
			}
			auto image = encoder.take();
			write(paged_directory / "storage.meta", l::encode_storage_identity(storage));
			write(paged_directory / (name + ".image"), image.bytes()->view());
			write(paged_directory / (name + ".lease"),
				  l::encode_generation_ownership(
					  {storage, id, image.descriptor().length, image.descriptor().checksum, 2}));
			for (const auto &segment : image.segments()) {
				const l::detail::SegmentNames names(segment.ownership.segment);
				write(paged_directory / names.payload_name(), segment.bytes->view());
				write(paged_directory / names.owner_name(), l::encode_segment_ownership(segment.ownership));
			}
			write(paged_directory / "selected", l::encode_descriptor(image.descriptor()));
			auto paged_base = l::recover_generation(paged_directory);
			for (bool progress : {false, true})
				for (unsigned tx : {1u, 2u, 17u}) {
					auto input = delta(progress, tx);
					auto next = id;
					next.fill('p');
					l::ImageBuilder applied(input, paged_base, next);
					complete(applied);
					l::ImageBuilder oracle(expected(progress, tx), next);
					complete(oracle);
					require(applied.base_descriptor() == image.descriptor());
					require(applied.descriptor() == oracle.descriptor());
					require(applied.bytes()->view().size() == oracle.bytes()->view().size());
					require(std::equal(applied.bytes()->view().begin(), applied.bytes()->view().end(),
									   oracle.bytes()->view().begin()));
				}
			for (unsigned partial : {0u, 1u, 1000u, 20000u, 1000000u}) {
				auto changes = delta(false, 17);
				auto next = id;
				next.fill('q');
				l::ImageBuilder cancelled(changes, paged_base, next);
				for (unsigned turns = 0; turns < partial && !cancelled.done(); ++turns) {
					l::ValidationBudget budget{1, 1};
					cancelled.step(budget);
				}
				unsigned turns = 0;
				while (true) {
					l::ValidationBudget budget{1, 1};
					if (cancelled.discard(budget))
						break;
					require(++turns < 5000000);
				}
				require(!cancelled.done());
				require(paged_base.view().get("app", "count").value() == INT64_MIN);
			}
		}

		{
			auto input = delta(false);
			auto account = input->storage_account();
			const auto before = account->used();
			{
				l::ImageBuilder builder(input, l::recover_generation(directory), id);
				const auto controls = account->used();
				auto pressure = account->admit(account->limit() - controls);
				bool rejected = false;
				try {
					l::ValidationBudget budget;
					builder.step(budget);
				} catch (const d::StoragePressure &) {
					rejected = true;
				}
				require(rejected && account->used() == account->limit());
				pressure.reset();
				while (true) {
					l::ValidationBudget budget{1, 1};
					if (builder.discard(budget))
						break;
				}
				require(account->used() == controls && !builder.done());
			}
			require(account->used() == before);
		}
		{
			auto input = delta(false);
			l::ViewLimits limits;
			limits.records = 0;
			l::ImageBuilder builder(input, l::recover_generation(directory), id, limits);
			bool rejected = false;
			try {
				complete(builder);
			} catch (const std::length_error &) {
				rejected = true;
			}
			require(rejected);
			std::size_t turns = 0;
			while (true) {
				l::ValidationBudget budget{1, 1};
				if (builder.discard(budget))
					break;
				require(++turns < 5000000);
			}
		}
		std::filesystem::remove_all(directory);
		std::cout << "exact-base repeated updates, deletion and progress passed\n";
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
