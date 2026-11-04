#include "detent_testing/generation_writer.hh"
#include "detent/json_schema.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/protocol.hh"
#include "detent_distribution/wire_encoder.hh"
#include "detent_local/disk_store.hh"
#include "managed_recovery.hh"
#include "paged_snapshot_plan.hh"
#include "detent_testing/generation_writer.h"
#include <completion/native_completion.h>
#include <fstream>
#include <map>
#include <random>
#include <sys/stat.h>
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
			if (const auto *flag = std::get_if<BoolDef>(def))
				for (const auto &[group, value] : flag->overrides)
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
			if (const auto *flag = std::get_if<BoolDef>(def)) {
				m::array(out, 4);
				m::unsigned_integer(out, 0);
				out.push_back(flag->default_value ? '\xc3' : '\xc2');
				if (flag->rollout_pct)
					m::unsigned_integer(out, *flag->rollout_pct);
				else
					out.push_back('\xc0');
				m::array(out, flag->overrides.size());
				for (const auto &[group, value] : flag->overrides)
					d::detail::wire::override_row(out, group, value);
			} else {
				m::array(out, 2);
				m::unsigned_integer(out, 1);
				m::signed_integer(out, std::get<IntDef>(*def).default_value);
			}
		}
		++index;
	}
	return out;
}
void unwrap(std::exception_ptr error) {
	if (error)
		std::rethrow_exception(error);
}
} // namespace
struct GenerationWriter::Impl {
	std::filesystem::path store;
	std::shared_ptr<d::StorageLedger> baseline = std::make_shared<d::StorageLedger>(segment);
	std::shared_ptr<d::StorageLedger> heap = std::make_shared<d::StorageLedger>(64 * segment);
	d::RetirementRuntime runtime{baseline, 1};
	d::RetirementDomain domain = runtime.domain(heap, 256);
	l::DiskStoreOptions options;
	std::shared_ptr<l::DiskStore> disk;
	l::StorageIdentity identity{};
	d::Cursor cursor;
	std::mt19937_64 random{std::random_device{}()};

	explicit Impl(std::filesystem::path path) : store(std::move(path)) {
		std::filesystem::create_directories(store);
		::chmod(store.c_str(), 0700);
		options.mode = std::filesystem::exists(store / "storage.meta") ? l::StoreOpenMode::ExistingManaged
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
		if (disk->status() != l::StoreStatus::Ready)
			throw std::runtime_error("test store did not open: " + store.string());
		std::array<char, l::storage_identity_bytes> bytes{};
		std::ifstream in(store / "storage.meta", std::ios::binary);
		in.read(bytes.data(), bytes.size());
		identity = l::decode_storage_identity(bytes);
		if (auto selected = disk->selection().descriptor)
			cursor.revision = selected->cursor.revision;
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
		namespace c = kronuz::io::completion;
		for (unsigned turns = 0;; ++turns) {
			if (turns > 1'000'000)
				throw std::runtime_error("test store made no progress");
			disk->advance();
			auto operation = disk->operation();
			if (!operation) {
				if (disk->needs_work())
					continue;
				break;
			}
			operation->submitted();
			if (!operation->complete(
			        kronuz::journal::detail::execute_primitive(operation->io(), operation->request())))
				throw std::runtime_error("test store completion refused");
		}
		unwrap(disk->error());
	}
	std::uint64_t publish(const Snapshot &knobs) {
		auto next = cursor;
		++next.revision;
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
		std::optional<l::RecoveredGeneration> actual;
		if (disk->selection().descriptor)
			actual.emplace(l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk)));
		l::GenerationId generation{};
		for (auto &byte : generation)
			byte = static_cast<char>(random());
		l::detail::PagedSnapshotPlanBuilder builder(snapshot, actual, disk->directory_identity(), identity,
		                                            generation, domain.factory(), heap, options.recovery_limits);
		while (!builder.done()) {
			l::ValidationBudget budget;
			builder.step(budget);
		}
		disk->begin(builder.take());
		drive();
		if (disk->publication_status() != l::DiskPublicationStatus::Published)
			throw std::runtime_error("test publication was refused");
		auto current = l::detail::ManagedRecovery::recover(store, l::detail::ManagedRecovery::capture(*disk));
		disk->confirm_recovered(current);
		drive();
		cursor = next;
		return cursor.revision;
	}
};
GenerationWriter::GenerationWriter(std::filesystem::path store) : impl_(std::make_unique<Impl>(std::move(store))) {}
GenerationWriter::~GenerationWriter() = default;
std::uint64_t GenerationWriter::publish(const Snapshot &knobs) { return impl_->publish(knobs); }
std::uint64_t GenerationWriter::publish_json(std::string_view json) {
	auto parsed = parse_defaults(json, "test");
	if (!parsed)
		throw std::invalid_argument("invalid knob definitions");
	return impl_->publish(parsed.value());
}
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
		handle->writer = std::make_unique<detent::testing::GenerationWriter>(store);
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
