#include "pinned_source.hh"
#include "snapshot_fixture.hh"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>

namespace l = detent::local;
namespace {
void write(const std::filesystem::path &path, std::span<const char> bytes) {
	std::ofstream stream(path, std::ios::binary);
	stream.write(bytes.data(), bytes.size());
	stream.close();
	require(static_cast<bool>(stream));
	std::filesystem::permissions(path,
								 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
template <class Function> void rejects(Function function) {
	bool rejected = false;
	try {
		function();
	} catch (const std::out_of_range &) {
		rejected = true;
	}
	require(rejected);
}
} // namespace
int main() {
	try {
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("pinned-source-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
		l::GenerationId generation;
		generation.fill('p');
		l::ImageBuilder builder(snapshot, generation);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		const auto name = l::generation_name(generation);
		write(directory / (name + ".image"), {builder.bytes()->data(), builder.bytes()->size()});
		write(directory / (name + ".lease"), {});
		write(directory / "selected", l::encode_descriptor(builder.descriptor()));
		auto baseline = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto heap = std::make_shared<d::StorageLedger>(1024 * 1024);
		auto mappings = std::make_shared<d::StorageLedger>(1024 * 1024);
		d::RetirementRuntime runtime(baseline, 1);
		auto domain = runtime.domain(heap, 2);
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		std::optional<l::detail::PinnedImageSource> source;
		source.emplace(l::recover_generation(directory, limits));
		require(source->descriptor() == builder.descriptor());
		require(source->namespace_count() == 1 && source->record_count() == 2 &&
				source->override_count() == 1);
		const auto resident = heap->used(), mapped = mappings->used();
		{
			auto pressure = heap->admit(heap->limit() - resident);
			require(source->name_space(0).name == "app" && source->name_space(0).revision == 42);
			const auto integer = source->record(0);
			require(integer.key == "app:count" && integer.namespace_index == 0 && integer.revision == 41);
			require(std::get<detent::IntDef>(integer.definition).default_value ==
					std::numeric_limits<std::int64_t>::min());
			const auto boolean = source->record(1);
			const auto definition = std::get<l::detail::SourceBoolean>(boolean.definition);
			require(boolean.key == "app:flag" && boolean.revision == 42 && definition.default_value &&
					definition.rollout_pct == 25);
			require(definition.override_start == 0 && definition.override_count == 1);
			const auto group = source->override_value(0);
			require(group.name == "beta" && std::get<detent::GroupRollout>(group.value).rollout_pct == 17);
			rejects([&] { source->name_space(1); });
			rejects([&] { source->record(2); });
			rejects([&] { source->override_value(1); });
			rejects([&] { source->override_value(std::numeric_limits<std::uint64_t>::max()); });
			require(heap->used() == heap->limit() && mappings->used() == mapped);
		}
		const auto lease = ::open((directory / (name + ".lease")).c_str(), O_RDONLY | O_CLOEXEC);
		require(lease >= 0 && ::flock(lease, LOCK_EX | LOCK_NB) < 0 &&
				(errno == EWOULDBLOCK || errno == EAGAIN));
		{
			const auto retained = *source;
			source.reset();
			require(retained.record(0).key == "app:count");
			require(::flock(lease, LOCK_EX | LOCK_NB) < 0 && (errno == EWOULDBLOCK || errno == EAGAIN));
		}
		domain.close().wait();
		require(mappings->used() == 0 && ::flock(lease, LOCK_EX | LOCK_NB) == 0);
		require(::close(lease) == 0);
		runtime.shutdown();
		std::filesystem::remove_all(directory);
		std::cout << "pinned namespace/record/override iteration, saturation and lease retirement passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
