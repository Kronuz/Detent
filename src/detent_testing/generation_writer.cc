#include "detent_testing/generation_writer.hh"
#include "detent/json_schema.hh"
#include "detent_distribution/frame_input.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/wire_encoder.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_compaction_plan.hh"
#include "paged_delta_plan.hh"
#include "paged_limits.hh"
#include "paged_snapshot_plan.hh"
#include "detent_testing/generation_writer.h"
#include <completion/native_completion.h>
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace detent::testing {
namespace d = distribution;
namespace l = local;
namespace m = d::messagepack;
namespace {
constexpr std::size_t segment = l::paged::segment_bytes;
// One snapshot in the distribution wire form, grouped by namespace and sorted by name.
std::string encode(const Snapshot &knobs, const d::Cursor &cursor, std::vector<std::string> &namespaces) {
	std::map<std::string, std::vector<std::pair<std::string_view, const KnobDef *>>> grouped;
	for (const auto &[name, def] : knobs.knobs) {
		const std::string_view full = name;
		const auto colon = full.find(':');
		if (colon == full.npos)
			throw std::invalid_argument("knob name needs a namespace: " + std::string(full));
		grouped[std::string(full.substr(0, colon))].emplace_back(full, &def);
	}
	std::uint64_t records = 0, overrides = 0, strings = 0;
	for (const auto &[ns, rows] : grouped) {
		strings += ns.size();
		namespaces.push_back(ns);
		for (const auto &[name, def] : rows) {
			++records;
			strings += name.size();
			for (const auto &[group, value] : def->overrides)
				++overrides, strings += group.size();
		}
	}
	std::string out;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 5);
	for (const auto *field : {&cursor.cluster, &cursor.group, &cursor.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(cursor.scope.data(), cursor.scope.size()), true);
	m::unsigned_integer(out, cursor.revision);
	m::array(out, 4);
	m::unsigned_integer(out, grouped.size());
	m::unsigned_integer(out, records);
	m::unsigned_integer(out, overrides);
	m::unsigned_integer(out, strings);
	m::array(out, grouped.size());
	for (const auto &[ns, rows] : grouped) {
		m::array(out, 2);
		m::string(out, ns);
		m::unsigned_integer(out, cursor.revision);
	}
	m::array(out, records);
	std::uint64_t index = 0;
	for (const auto &[ns, rows] : grouped) {
		for (const auto &[name, def] : rows) {
			m::array(out, 4);
			m::unsigned_integer(out, index);
			m::string(out, name);
			m::unsigned_integer(out, cursor.revision);
			d::detail::wire::definition(out, d::WireDefinition{def->default_value,
															   {0, def->overrides.size()},
															   def->rollout_pct,
															   def->rollout_value});
			for (const auto &[group, value] : def->overrides)
				d::detail::wire::override_row(out, group, value);
		}
		++index;
	}
	return out;
}
// A namespace's own cursor: its scope, and so its incarnation, is the name's, as on a delivery frame.
d::Cursor own_cursor(const d::Cursor &cursor, std::string_view name) {
	auto own = cursor;
	own.scope = d::frame_cursor(name, d::namespace_cursor(cursor)).scope;
	return own;
}
void unwrap(std::exception_ptr error) {
	if (error)
		std::rethrow_exception(error);
}
} // namespace
struct GenerationWriter::Impl {
	std::filesystem::path store;
	Layout layout;
	std::shared_ptr<d::StorageLedger> baseline = std::make_shared<d::StorageLedger>(segment);
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(64 * segment);
	d::RetirementRuntime runtime{baseline, 1};
	d::RetirementDomain domain = runtime.domain(heap, 1 << 16); // owners: each namespace keeps its recovered head
	l::DiskStoreOptions options;
	std::shared_ptr<l::DiskStore> disk;
	d::Cursor cursor;

	Impl(std::filesystem::path path, Layout table) : store(std::move(path)), layout(table) {
		std::filesystem::create_directories(store);
		::chmod(store.c_str(), 0700);
		options.mode = std::filesystem::exists(store / "owner.lock") ? l::StoreOpenMode::ExistingManaged
		                                                             : l::StoreOpenMode::FreshEmpty;
		options.limits = {1024 * segment, 100000, 1000};
		options.recovery_limits.retirement_domain = &domain;
		options.recovery_limits.mapping_account = std::make_shared<d::StorageLedger>(64 * segment);
		cursor.cluster.fill('c');
		cursor.group.fill('g');
		cursor.restore_epoch.fill('e');
		cursor.scope.fill('s');
		disk = l::make_disk_store(domain, store, std::make_shared<int>(1), options);
		drive();
		// The writer continues only an intact generation: a failed entry is the agent's to repair.
		if (disk->status() != l::StoreStatus::Ready || !disk->failed().empty())
			throw std::runtime_error("test store did not open: " + store.string());
		for (const auto &entry : disk->selection().namespaces)
			cursor.revision = std::max(cursor.revision, entry.descriptor.cursor.position);
	}
	~Impl() {
		disk->close();
		try {
			drive();
		} catch (...) {
		}
		disk.reset();
		domain.close().wait();
	}
	void drive() {
		for (unsigned turns = 0;; ++turns) {
			if (turns > 1'000'000)
				throw std::runtime_error("test store made no progress");
			disk->advance();
			auto work = disk->work();
			if (!work)
				break;
			work->run();
		}
		disk->advance();
		unwrap(disk->error());
	}
	// One snapshot plan of `knobs` at `next` over entry `name`'s generation (empty: the only entry).
	l::detail::PagedPlan plan(const Snapshot &knobs, const d::Cursor &next, std::string_view name, bool repair) {
		std::vector<std::string> namespaces;
		const auto wire = encode(knobs, next, namespaces);
		d::Limits limits;
		limits.storage_account = heap;
		d::SnapshotDecoder decoder(next, namespaces, limits);
		std::span<const char> input(wire.data(), wire.size());
		while (!input.empty()) {
			d::WorkBudget budget{1 << 20, 1 << 16};
			decoder.feed(input, budget);
		}
		auto snapshot = std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
		const auto selection = disk->selection();
		std::optional<l::RecoveredGeneration> actual;
		if (name.empty() ? selection.descriptor.has_value() : selection.find(name) != nullptr)
			actual.emplace(l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk, name)));
		l::detail::PagedSnapshotPlanBuilder builder(snapshot, actual, disk->directory_identity(), domain.factory(),
		                                            heap, options.recovery_limits, repair);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		return builder.take();
	}
	// Settles a published cycle: recovers and confirms each namespace it published.
	void settle() {
		if (disk->publication_status() != l::DiskPublicationStatus::Published)
			throw std::runtime_error("test publication was refused");
		if (layout == Layout::OneEntry)
			disk->confirm_recovered(
			    l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk)));
		else
			for (const auto &name : disk->pending())
				disk->confirm_recovered(name, l::detail::ManagedRecovery::recover(
				                                  store, l::detail::ManagedRecovery::capture(*disk, name)));
		drive();
	}
	std::uint64_t publish(const Snapshot &knobs, bool repair = false) {
		auto next = cursor;
		if (!repair)
			++next.revision;
		if (layout == Layout::OneEntry) {
			disk->begin(plan(knobs, next, {}, repair));
			drive();
			settle();
		} else {
			// An entry per namespace (namespace-delivery D8), each at the new revision.
			std::map<std::string, Snapshot, std::less<>> split;
			for (const auto &[name, def] : knobs.knobs) {
				const std::string_view full = name;
				const auto colon = full.find(':');
				if (colon == full.npos)
					throw std::invalid_argument("knob name needs a namespace: " + std::string(full));
				split[std::string(full.substr(0, colon))].knobs.emplace(name, def);
			}
			std::vector<std::string> dropped;
			for (const auto &entry : disk->selection().namespaces)
				if (!split.contains(entry.name))
					dropped.push_back(entry.name);
			// Cycles of at most 16 plans keep the builds inside the writer's heap ledger.
			auto at = split.begin();
			do {
				std::vector<l::NamespacePublication> cycle;
				for (; at != split.end() && cycle.size() != 16; ++at)
					cycle.push_back({at->first, std::make_unique<l::detail::PagedPlan>(
					                                plan(at->second, own_cursor(next, at->first), at->first, repair))});
				disk->begin(std::move(cycle), std::exchange(dropped, {}));
				drive();
				settle();
			} while (at != split.end());
		}
		cursor = next;
		return cursor.revision;
	}
	void put_cursor(std::string &out, const d::Cursor &value) {
		m::array(out, 5);
		for (const auto *field : {&value.cluster, &value.group, &value.restore_epoch})
			m::string(out, std::string_view(field->data(), field->size()), true);
		m::string(out, std::string_view(value.scope.data(), value.scope.size()), true);
		m::unsigned_integer(out, value.revision);
	}
	void publish_plan(l::detail::PagedPlan built, std::string_view name = {}) {
		if (name.empty())
			disk->begin(std::move(built));
		else {
			std::vector<l::NamespacePublication> cycle;
			cycle.push_back({std::string(name), std::make_unique<l::detail::PagedPlan>(std::move(built))});
			disk->begin(std::move(cycle));
		}
		drive();
		settle();
	}
	std::uint64_t delta(const std::vector<std::string> &scope, std::string_view name_space,
	                    const std::map<std::string, std::int64_t> &upserts,
	                    const std::vector<std::string> &removals) {
		if (std::find(scope.begin(), scope.end(), name_space) == scope.end())
			throw std::invalid_argument("delta namespace is not in the scope");
		// PerNamespace: the namespace's own entry, from its own revision.
		const bool own = layout == Layout::PerNamespace;
		const std::vector<std::string> alone{std::string(name_space)};
		const auto &names = own ? alone : scope;
		const std::string_view entry = own ? name_space : std::string_view();
		const auto namespace_index =
		    static_cast<std::uint64_t>(std::find(names.begin(), names.end(), name_space) - names.begin());
		auto base = own ? own_cursor(cursor, name_space) : cursor;
		if (own) {
			const auto selection = disk->selection();
			const auto *selected = selection.find(name_space);
			if (!selected)
				throw std::invalid_argument("delta namespace has no entry");
			base.revision = selected->cursor.position;
		}
		auto next = base;
		next.revision = cursor.revision + 1;
		std::map<std::string, std::optional<std::int64_t>> changes;
		for (const auto &[name, value] : upserts)
			changes[name] = value;
		for (const auto &name : removals)
			changes[name] = std::nullopt;
		std::uint64_t strings = name_space.size();
		for (const auto &[name, value] : changes)
			strings += name.size();
		std::string wire;
		m::array(wire, 7);
		m::unsigned_integer(wire, 1);
		m::unsigned_integer(wire, 2);
		m::array(wire, 2);
		m::unsigned_integer(wire, 1);
		m::unsigned_integer(wire, 1);
		put_cursor(wire, base);
		put_cursor(wire, next);
		m::array(wire, 4);
		m::unsigned_integer(wire, 1);
		m::unsigned_integer(wire, changes.size());
		m::unsigned_integer(wire, 0);
		m::unsigned_integer(wire, strings);
		m::array(wire, 1);
		m::array(wire, 3);
		m::unsigned_integer(wire, next.revision);
		m::array(wire, 1);
		m::array(wire, 2);
		m::string(wire, name_space);
		m::unsigned_integer(wire, next.revision);
		m::array(wire, changes.size());
		for (const auto &[name, value] : changes) {
			m::array(wire, 4);
			m::unsigned_integer(wire, namespace_index);
			m::string(wire, name);
			m::unsigned_integer(wire, value ? 0 : 1);
			if (value)
				d::detail::wire::definition(wire, d::WireDefinition{*value, {}, {}, {}});
			else
				wire.push_back('\xc0');
		}
		d::Limits limits;
		limits.storage_account = heap;
		d::DeltaDecoder decoder(base, names, limits, d::UpdateKind::Delta);
		std::span<const char> input(wire.data(), wire.size());
		while (!input.empty()) {
			d::WorkBudget budget{1 << 20, 1 << 16};
			decoder.feed(input, budget);
		}
		auto source = std::make_shared<const d::ValidatedDelta>(decoder.finish());
		auto actual = l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk, entry));
		const auto bounds = l::detail::paged_limits(options.recovery_limits, l::paged::root_bytes);
		l::detail::PagedDeltaPlanBuilder builder(actual, source, domain.factory(), heap, bounds);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		publish_plan(builder.take(), entry);
		cursor.revision = next.revision;
		return cursor.revision;
	}
	void compact(std::size_t packs) {
		if (layout != Layout::OneEntry)
			throw std::logic_error("compaction needs the one-entry layout");
		const auto context = l::detail::ManagedRecovery::capture(*disk);
		auto actual = l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk));
		std::array<l::SegmentId, 8> selected{};
		std::size_t count = 0;
		const auto prior = context.paged.prior;
		if (!prior)
			throw std::logic_error("compaction needs a recovered head");
		const auto inputs = prior->registry()->inputs();
		for (std::size_t i = 0; i < inputs.size() && count < std::min(packs, selected.size()); ++i)
			if (prior->registry()->reachability(i).pages)
				selected[count++] = inputs[i].ownership.segment;
		const auto bounds = l::detail::paged_limits(options.recovery_limits, l::paged::root_bytes);
		l::detail::PagedCompactionBuilder builder(actual, {selected.data(), count}, domain.factory(), heap,
		                                          l::detail::CompactionLimits{}, bounds);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		publish_plan(builder.take());
	}
};
GenerationWriter::GenerationWriter(std::filesystem::path store, Layout layout)
    : impl_(std::make_unique<Impl>(std::move(store), layout)) {}
GenerationWriter::~GenerationWriter() = default;
std::uint64_t GenerationWriter::publish(const Snapshot &knobs) { return impl_->publish(knobs); }
std::uint64_t GenerationWriter::publish_json(std::string_view json) {
	auto parsed = parse_defaults(json, "test");
	if (!parsed)
		throw std::invalid_argument("invalid knob definitions");
	return impl_->publish(parsed.value());
}
std::uint64_t GenerationWriter::repair_json(std::string_view json) {
	auto parsed = parse_defaults(json, "test");
	if (!parsed)
		throw std::invalid_argument("invalid knob definitions");
	return impl_->publish(parsed.value(), true);
}
void GenerationWriter::collect() {
	impl_->disk->request_collection();
	impl_->drive();
}
std::uint64_t GenerationWriter::publish_delta(const std::vector<std::string> &scope, std::string_view name_space,
                                              const std::map<std::string, std::int64_t> &upserts,
                                              const std::vector<std::string> &removals) {
	return impl_->delta(scope, name_space, upserts, removals);
}
void GenerationWriter::compact(std::size_t packs) { impl_->compact(packs); }
std::uint64_t GenerationWriter::revision() const noexcept { return impl_->cursor.revision; }
const std::filesystem::path &GenerationWriter::store() const noexcept { return impl_->store; }
} // namespace detent::testing

struct detent_testing_writer {
	std::unique_ptr<detent::testing::GenerationWriter> writer;
	std::string error;
};
namespace {
thread_local std::string open_error;
}
extern "C" {
detent_testing_writer *detent_testing_open(const char *store) {
	try {
		auto handle = std::make_unique<detent_testing_writer>();
		handle->writer = std::make_unique<detent::testing::GenerationWriter>(
		    store, detent::testing::GenerationWriter::Layout::PerNamespace);
		return handle.release();
	} catch (const std::exception &error) {
		open_error = error.what();
		return nullptr;
	}
}
const char *detent_testing_open_error(void) { return open_error.c_str(); }
int detent_testing_publish(detent_testing_writer *writer, const char *json, size_t length, uint64_t *revision) {
	try {
		const auto value = writer->writer->publish_json({json, length});
		if (revision)
			*revision = value;
		return 0;
	} catch (const std::exception &error) {
		writer->error = error.what();
		return -1;
	}
}
const char *detent_testing_error(const detent_testing_writer *writer) { return writer->error.c_str(); }
void detent_testing_close(detent_testing_writer *writer) { delete writer; }
}
