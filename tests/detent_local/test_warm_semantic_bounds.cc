// A delta plan whose store passes 128 MiB of string bytes builds: the finalizer's warm validation
// checks the format's semantic bounds. A store with default recovery limits then refuses to publish it.
#include "applied_delta_fixture.hh"
#include "detent_local/disk_store.hh"
#include "paged_delta_plan.hh"
#include "paged_disk_fixture.hh"
#include <completion/native_completion.h>
namespace p = l::paged;
namespace {
constexpr unsigned definitions = 12000, overrides = 256, key_bytes = 45, name_bytes = 11;
// Every definition is new, its default 0, with `overrides` forced rows of `key_bytes`-byte keys.
std::string wide_delta() {
	std::string wire;
	auto a = [&](unsigned n) { m::array(wire, n); };
	auto n = [&](std::uint64_t v) { m::unsigned_integer(wire, v); };
	auto text = [&](std::string_view v) { m::string(wire, v); };
	a(7);
	n(1);
	n(2);
	a(2);
	n(1);
	n(1);
	put_cursor(wire, cursor());
	auto target = cursor();
	target.revision = 43;
	put_cursor(wire, target);
	a(4);
	n(1);
	n(definitions);
	n(std::uint64_t(definitions) * overrides);
	n(std::uint64_t(definitions) * (name_bytes + overrides * key_bytes) + 3);
	a(1);
	a(3);
	n(43);
	a(1);
	a(2);
	text("app");
	n(43);
	a(definitions);
	std::string key(key_bytes, 'x');
	for (unsigned i = 0; i < definitions; ++i) {
		const auto digits = std::to_string(i);
		a(4);
		n(0);
		text("app:n" + std::string(6 - digits.size(), '0') + digits);
		n(0);
		a(4);
		n(0);
		wire.push_back('\xc0');
		wire.push_back('\xc0');
		a(overrides);
		for (unsigned row = 0; row < overrides; ++row) {
			const auto label = std::to_string(row);
			key.replace(0, 4, "g" + std::string(3 - label.size(), '0') + label);
			a(3);
			text(key);
			n(0);
			n(1);
		}
	}
	return wire;
}
} // namespace
int main() {
	const auto directory = std::filesystem::current_path() / ".scratch" /
						   ("warm-semantic-bounds-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{directory};
	auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decode(fixture()));
	l::StorageIdentity storage{}, generation{}, next{};
	storage.fill('s');
	generation.fill('g');
	next.fill('n');
	l::detail::PagedSnapshotBuilder original(snapshot, storage, generation);
	while (!original.done()) {
		l::ValidationBudget work;
		original.step(work);
	}
	install(directory, original.take(), storage);
	write(directory / "owner.lock", {});
	auto recovered = l::recover_generation(directory);
	auto account = std::make_shared<d::StorageLedger>(4ull << 30);
	d::RetirementRuntime runtime(account, 1);
	auto domain = runtime.domain(account, 64);
	{
		d::Limits limits;
		limits.bytes = limits.storage_bytes = 1ull << 30;
		limits.string_bytes = 512ull << 20;
		limits.storage_account = account;
		d::DeltaDecoder decoder(cursor(), {"app"}, limits);
		{
			const auto wire = wide_delta();
			auto input = std::span<const char>(wire);
			while (!input.empty()) {
				d::WorkBudget work{1 << 20, 1 << 16};
				decoder.feed(input, work);
			}
		}
		p::Limits bounds;
		bounds.bytes = 1ull << 30;
		l::detail::PagedDeltaPlanBuilder builder(recovered, decoder.finish(), next, domain, bounds);
		while (!builder.done()) {
			l::ValidationBudget work;
			builder.step(work);
		}
		auto plan = builder.take();
		// The fixture's base holds 24 string bytes, "app" among them.
		const auto strings = std::uint64_t(definitions) * (name_bytes + overrides * key_bytes) + 24;
		require(strings > l::ViewLimits{}.strings);
		require(plan.counts().records == definitions + 2 && plan.counts().strings == strings &&
				plan.counts().maximum_overrides == overrides &&
				plan.override_histogram()[overrides] == definitions);
		std::cout << "delta plan with " << plan.counts().strings << " string bytes built\n";
		// Publication admission is the store's only string check: default limits refuse the plan.
		l::DiskStoreOptions options;
		options.limits = {1ull << 30, 100, 10};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = std::make_shared<d::StorageLedger>(1ull << 30);
		auto store = l::make_disk_store(domain, directory, std::make_shared<int>(1), options);
		while (store->operation() || store->needs_work()) {
			auto job = store->operation();
			if (!job) {
				store->advance();
				continue;
			}
			job->submitted();
			require(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
			store->advance();
		}
		require(store->status() == l::StoreStatus::Ready && store->publication_ready());
		const auto wanted = plan.descriptor();
		try {
			store->begin(std::move(plan));
			throw std::runtime_error("a plan over the store's string limit was admitted");
		} catch (const std::invalid_argument &error) {
			require(std::string_view(error.what()) ==
					"managed paged publication exact base or admission mismatch");
		}
		require(plan.descriptor() == wanted && store->publication_ready());
		std::cout << "default recovery limits refuse it\n";
	}
	domain.close().wait();
}
