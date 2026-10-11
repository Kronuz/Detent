#include "detent_local/reader.hh"
#include "paged_plan.hh"
#include "paged_progress_fixture.hh"
#include <chrono>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
template <class F> void wait_for(F predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!predicate()) {
		require(std::chrono::steady_clock::now() < deadline);
		std::this_thread::yield();
	}
}
template <class F> void rejects(F action) {
	try {
		action();
	} catch (const std::exception &) {
		return;
	}
	throw std::runtime_error("unsafe warm recovery accepted");
}
int main() {
	auto directory = std::filesystem::current_path() / ".scratch" /
					 ("paged-progress-recovery-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code e;
			std::filesystem::remove_all(path, e);
		}
	} cleanup{directory};
	auto source = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder snapshot(source, storage, generation);
	while (!snapshot.done()) {
		l::ValidationBudget b;
		snapshot.step(b);
	}
	auto image = snapshot.take();
	install(directory, image, storage);
	auto heap = std::make_shared<d::StorageLedger>(8 * 1024 * 1024);
	auto mappings = std::make_shared<d::StorageLedger>(1024 * 1024);
	{
		d::RetirementRuntime runtime(heap, 1);
		auto domain = runtime.domain(heap, 16384);
		l::ViewLimits limits;
		limits.retirement_domain = &domain;
		limits.mapping_account = mappings;
		auto base = std::optional<l::RecoveredGeneration>(l::recover_generation(directory, limits));
		const auto baseline = mappings->used();
		const auto page = std::size_t(::sysconf(_SC_PAGESIZE));
		{
			auto delta = progress(base->descriptor().cursor, heap);
			l::detail::PagedPlanBuilder builder(*base, delta, next, domain);
			while (!builder.done()) {
				l::ValidationBudget b;
				builder.step(b);
			}
			auto plan = builder.take();
			const auto descriptor = plan.descriptor();
			const auto name = l::generation_name(next);
			write(directory / (name + ".image"), {plan.bytes()->data(), plan.bytes()->size()});
			write(directory / (name + ".lease"),
				  l::encode_generation_ownership({storage, next, descriptor.length, descriptor.checksum, 2}));
			write(directory / "selected", l::encode_descriptor(descriptor));
		}
		std::optional<l::RecoveredGeneration> current;
		{
			auto pressure = mappings->admit(mappings->limit() - baseline - page);
			current.emplace(l::recover_generation(directory, limits, {}, &*base));
			require(current->view().cursor().revision == 45 && current->view().size() == 2);
			require(current->view().get("app", "count").value() == std::numeric_limits<std::int64_t>::min());
			current.reset();
			wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
			rejects([&] { l::recover_generation(directory, limits); });
			wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
			auto strict = limits;
			strict.overrides = 0;
			rejects([&] { l::recover_generation(directory, strict, {}, &*base); });
			wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
			strict = limits;
			strict.strings = 23;
			rejects([&] { l::recover_generation(directory, strict, {}, &*base); });
			wait_for([&] { return mappings->used() == baseline + pressure.bytes(); });
			current.emplace(l::recover_generation(directory, limits, {}, &*base));
		}
		const l::detail::SegmentNames names(image.segments()[0].ownership.segment);
		const auto owner = l::encode_segment_ownership(image.segments()[0].ownership);
		auto mismatched = image.segments()[0].ownership;
		mismatched.checksum[0] ^= 1;
		write(directory / names.owner_name(), l::encode_segment_ownership(mismatched));
		rejects([&] { l::recover_generation(directory, limits, {}, &*current); });
		write(directory / names.owner_name(), owner);
		require(current->view().eval("app", "flag", {}, "beta").has_value());
		base.reset();
		const int lease = ::open((directory / (l::generation_name(generation) + ".lease")).c_str(), O_RDWR);
		require(lease >= 0);
		wait_for([&] { return ::flock(lease, LOCK_EX | LOCK_NB) == 0; });
		::close(lease);
		{
			l::GenerationReader reader(directory, limits);
			require(reader.refresh());
			require(reader.read_view().cursor().revision == 45);
			auto delta = progress(current->descriptor().cursor, heap);
			l::GenerationId later;
			later.fill('p');
			l::detail::PagedPlanBuilder builder(*current, delta, later, domain);
			while (!builder.done()) {
				l::ValidationBudget b;
				builder.step(b);
			}
			{
				auto plan = builder.take();
				const auto descriptor = plan.descriptor();
				const auto name = l::generation_name(later);
				write(directory / (name + ".image"), {plan.bytes()->data(), plan.bytes()->size()});
				write(directory / (name + ".lease"),
					  l::encode_generation_ownership(
						  {storage, later, descriptor.length, descriptor.checksum, 2}));
				write(directory / "selected", l::encode_descriptor(descriptor));
			}
			const auto installed_bytes = mappings->used();
			auto pressure = mappings->admit(mappings->limit() - installed_bytes - page);
			require(reader.refresh());
			require(reader.read_view().cursor().revision == 48 && !reader.refresh());
			require(reader.read_view().get("app", "count").has_value());
		}
		std::filesystem::remove_all(directory);
		require(current->view().get("app", "count").has_value());
		current.reset();
		auto drain = domain.close();
		wait_for([&] { return drain.done(); });
		runtime.shutdown();
	}
	require(heap->used() == 0);
	require(mappings->used() == 0);
}
