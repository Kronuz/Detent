// Exports a 5M corpus (DTCORP01) as a managed paged store of 1 or 1,000 namespaces, written with
// the tree's own builder, so one source builds the baseline and the Dense store. The cursor and
// scope match the agent configuration the first-read gate writes (docs/perf/first-read-gate.md).
// Usage: first_read_export CORPUS OUTPUT NAMESPACES LAYOUT_VERSION
#include "change_file.hh"
#include "detent/scope.hh"
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/paged_definition.hh"
#include "paged_snapshot_builder.hh"
#include "segment_names.hh"
#include "trusted_records.hh"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>

namespace d = detent::distribution;
namespace l = detent::local;
namespace p = detent::local::paged;
namespace x = detent::local::detail;

namespace {
void require(bool value, const char *what) {
	if (!value)
		throw std::runtime_error(what);
}
struct Row {
	std::string key;
	std::vector<char> body;
};
using Rows = std::vector<Row>;
std::shared_ptr<Rows> corpus(const std::string &path) {
	std::ifstream file(path, std::ios::binary);
	require(bool(file), "cannot open corpus");
	const std::vector<char> data{std::istreambuf_iterator<char>(file), {}};
	require(data.size() >= 32 && std::string_view(data.data(), 8) == "DTCORP01", "not a DTCORP01 corpus");
	const auto count = l::read_integer(data, 8, 8), keys = l::read_integer(data, 16, 8),
			   bodies = l::read_integer(data, 24, 8);
	const auto key_at = 32 + 4 * (count + 1), body_offsets = key_at + keys, body_at = body_offsets + 4 * (count + 1);
	require(body_at + bodies + count == data.size(), "corpus size");
	auto rows = std::make_shared<Rows>();
	rows->reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		const auto k0 = l::read_integer(data, 32 + 4 * i, 4), k1 = l::read_integer(data, 36 + 4 * i, 4),
				   b0 = l::read_integer(data, body_offsets + 4 * i, 4),
				   b1 = l::read_integer(data, body_offsets + 4 * i + 4, 4);
		require(k0 <= k1 && k1 <= keys && b0 <= b1 && b1 <= bodies, "corpus offsets");
		rows->push_back({std::string(data.data() + key_at + k0, k1 - k0),
						 std::vector<char>(data.begin() + body_at + b0, data.begin() + body_at + b1)});
	}
	return rows;
}
// The same shape as test_dense_fullbuild's reshape: every record in `primary`, plus 999 side
// namespaces of 100 integer knobs for the 1,000-namespace store.
void reshape(Rows &rows, unsigned namespaces) {
	require(namespaces == 1 || namespaces == 1000, "namespaces must be 1 or 1000");
	std::uint64_t revision = 0;
	for (const auto &row : rows)
		if (row.key.front() == 'W')
			revision = std::max(revision, l::read_integer(row.body, 0, 8));
	std::erase_if(rows, [](const auto &row) { return row.key.front() == 'W'; });
	for (auto &row : rows)
		row.key = std::string("Kprimary\0", 9) + std::string(p::decode_key(row.key).local_name) + '\0';
	for (unsigned i = 0; i < namespaces; ++i) {
		const auto name = i == 0 ? std::string("primary") : "side" + std::to_string(i);
		std::vector<char> watermark(8);
		l::write_integer(watermark, 0, revision, 8);
		rows.push_back({"W" + name + '\0', std::move(watermark)});
		for (unsigned j = 0; i && j < 100; ++j) {
			std::vector<char> body(p::definition_header_bytes);
			p::encode_definition(body, detent::KnobDef{17}, revision);
			rows.push_back({"K" + name + '\0' + "key" + std::to_string(j) + '\0', std::move(body)});
		}
	}
	std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
}
d::Identity tagged(char tag) {
	d::Identity id{};
	id[0] = tag;
	return id;
}
class CorpusSource final : public d::SnapshotSource {
  public:
	CorpusSource(std::shared_ptr<Rows> rows, std::shared_ptr<d::StorageLedger> account)
		: rows_(std::move(rows)), account_(std::move(account)) {
		std::vector<std::string_view> names;
		for (const auto &row : *rows_)
			if (row.key.front() == 'W') {
				const auto key = p::decode_key(row.key);
				const auto revision = l::read_integer(row.body, 0, 8);
				namespaces_.push_back({key.name_space, revision});
				names.push_back(key.name_space);
				cursor_.revision = std::max(cursor_.revision, revision);
			}
		for (std::size_t i = 0; i < namespaces_.size(); ++i)
			indices_.emplace(namespaces_[i].name, i);
		// managed.json: cluster 63.., group 67.., epoch 65.., principal system-reader, policy epoch 1.
		cursor_.cluster = tagged('c');
		cursor_.group = tagged('g');
		cursor_.restore_epoch = tagged('e');
		std::sort(names.begin(), names.end());
		cursor_.scope = d::canonical_scope_identity("system-reader", 1, names);
	}
	const d::Cursor &cursor() const noexcept override { return cursor_; }
	std::shared_ptr<d::StorageLedger> storage_account() const noexcept override { return account_; }
	std::uint64_t namespace_count() const noexcept override { return namespaces_.size(); }
	d::NamespaceRevision name_space(std::uint64_t i) const override { return namespaces_.at(i); }
	void reset_records() noexcept override { ordinal_ = 0; }
	d::SnapshotRecordStep next_record() override {
		if (ordinal_ == rows_->size() || (*rows_)[ordinal_].key.front() == 'W')
			return {{}, true};
		const auto &row = (*rows_)[ordinal_++];
		const auto key = p::decode_key(row.key);
		const auto index = indices_.at(key.name_space);
		const auto def = p::validate_definition(row.body, namespaces_[index].revision);
		body_ = row.body;
		override_at_ = p::header_size(def);
		remaining_ = def.overrides;
		name_ = std::string(key.name_space) + ":" + std::string(key.local_name);
		const d::WireDefinition definition{def.default_value, {0, def.overrides}, def.rollout, def.rollout_value};
		return {d::Record{static_cast<std::uint32_t>(index), name_, def.revision, definition}, false};
	}
	std::optional<d::OverrideRow> next_override() override {
		if (!remaining_)
			return {};
		--remaining_;
		const auto value = p::read_override(body_, override_at_);
		return d::OverrideRow{value.key, value.value};
	}

  private:
	std::shared_ptr<Rows> rows_;
	std::shared_ptr<d::StorageLedger> account_;
	d::Cursor cursor_;
	std::vector<d::NamespaceRevision> namespaces_;
	std::map<std::string_view, std::size_t> indices_;
	std::string name_;
	std::span<const char> body_;
	std::size_t ordinal_ = 0, override_at_ = 0;
	std::uint32_t remaining_ = 0;
};
std::shared_ptr<const d::ValidatedSnapshot> admit(const std::shared_ptr<Rows> &rows,
												  const std::shared_ptr<d::StorageLedger> &account) {
	auto source = std::make_shared<CorpusSource>(rows, account);
	std::vector<std::string_view> selection;
	for (std::size_t i = 0; i < source->namespace_count(); ++i)
		selection.push_back(source->name_space(i).name);
	d::Limits limits;
	limits.bytes = 4ull * 1024 * 1024 * 1024;
	limits.string_bytes = limits.bytes;
	limits.records = rows->size();
	limits.namespaces = selection.size();
	limits.storage_account = account;
	limits.storage_bytes = account->limit();
	d::SnapshotDecoder decoder(source->cursor(), d::NamespaceSelection(selection), limits);
	{
		d::SnapshotEncoder encoder(source, limits);
		while (!encoder.done()) {
			d::WorkBudget work{65536, 256};
			encoder.step(work);
			if (encoder.chunk_ready()) {
				const auto chunk = encoder.take_chunk();
				std::span<const char> input(chunk->data(), chunk->size());
				while (!input.empty()) {
					d::WorkBudget decode{65536, 256};
					decoder.feed(input, decode);
				}
			}
		}
	}
	return std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
}
// The agent's cold start opens the existing owner lock and requires `.read` and `.live`; both are
// derived from one full recovery, as trusted_records::derive does, with limits sized to the corpus.
void agent_records(const std::filesystem::path &output, std::uint32_t records) {
	auto heap = std::make_shared<d::StorageLedger>(4ull << 30);
	d::RetirementRuntime runtime(heap, 1);
	auto domain = runtime.domain(heap, 16384);
	{
		l::ViewLimits limits;
		limits.records = records;
		limits.bytes = limits.strings = 4ull << 30;
		limits.pages = limits.bytes / p::page_header_bytes;
		limits.retirement_domain = &domain;
		limits.mapping_account = std::make_shared<d::StorageLedger>(4ull << 30);
		const auto generation = l::recover_generation(output, limits);
		const auto &registry = *x::RecoveredTesting::root(generation).registry();
		const auto metadata = x::encode_reader_metadata(x::PreparedReader::describe(generation));
		x::LiveRecord live;
		live.generation = generation.descriptor().generation;
		live.root_checksum = generation.descriptor().checksum;
		std::copy(metadata.end() - x::live_digest_bytes, metadata.end(), live.read_digest.begin());
		std::copy(registry.override_histogram().begin(), registry.override_histogram().end(), live.histogram.begin());
		for (std::size_t i = 0; i < registry.inputs().size(); ++i) {
			const auto &ownership = registry.inputs()[i].ownership;
			live.rows.push_back({ownership.segment, ownership.length, ownership.checksum, registry.reachability(i)});
		}
		std::vector<char> bytes(x::live_record_size(live.rows.size()));
		x::encode_live_record(live, bytes);
		const auto id = generation.descriptor().generation;
		trusted_records::put(output / std::string(x::GenerationName(id, x::GenerationFile::ReaderMetadata).view()),
							 metadata);
		trusted_records::put(output / std::string(x::GenerationName(id, x::GenerationFile::Live).view()), bytes);
	}
	domain.close().wait();
}
void run(const std::string &path, const std::filesystem::path &output, unsigned namespaces, std::uint32_t version) {
	auto rows = corpus(path);
	reshape(*rows, namespaces);
	auto account = std::make_shared<d::StorageLedger>(8ull * 1024 * 1024 * 1024);
	auto snapshot = admit(rows, account);
	p::Limits limits;
	limits.records = std::max<std::uint32_t>(limits.records, snapshot->records().size());
	limits.namespaces = std::max<std::uint32_t>(limits.namespaces, snapshot->namespaces().size());
	limits.pages = std::max<std::uint64_t>(limits.pages, 2 * rows->size());
	limits.bytes = 4ull * 1024 * 1024 * 1024;
	l::StorageIdentity storage{};
	l::GenerationId generation{};
	storage.fill('s');
	generation.fill('g');
	x::PagedSnapshotBuilder builder(snapshot, generation, limits);
	while (!builder.done()) {
		l::ValidationBudget budget{65536, 256};
		builder.step(budget);
	}
	auto image = builder.take();
	require(std::filesystem::create_directories(output), "output exists");
	std::filesystem::permissions(output, std::filesystem::perms::owner_all);
	trusted_records::put(output / "storage.meta", l::encode_storage_identity(storage));
	const auto name = l::generation_name(generation);
	trusted_records::put(output / (name + ".image"), image.bytes()->view());
	trusted_records::put(output / (name + ".lease"),
						 l::encode_generation_ownership({storage, generation, image.descriptor().length,
														 image.descriptor().checksum, version}));
	const auto descriptor = l::encode_descriptor(image.descriptor());
	trusted_records::put(output / (name + ".descriptor"), descriptor);
	for (const auto &segment : image.segments()) {
		const x::SegmentNames names(segment.ownership.segment);
		trusted_records::put(output / names.payload_name(), segment.bytes->view());
		trusted_records::put(output / names.owner_name(), l::encode_segment_ownership(segment.ownership));
	}
	trusted_records::put(output / "selected", descriptor);
	const int directory = ::open(output.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	require(directory >= 0, "cannot open output");
	{
		x::ChangeWriter change(directory, 1, ::getuid(), 0600);
		change.bump_all();
	}
	require(::close(directory) == 0, "close output");
	trusted_records::put(output / "owner.lock", {});
	agent_records(output, snapshot->records().size());
	std::cout << "{\"records\":" << snapshot->records().size() << ",\"namespaces\":" << snapshot->namespaces().size()
			  << ",\"segments\":" << image.segments().size() << ",\"layout_version\":" << version << "}\n";
	l::ValidationBudget disposal{SIZE_MAX, SIZE_MAX};
	require(image.discard(disposal), "image discard");
}
} // namespace

int main(int argc, char **argv) try {
	require(argc == 5, "usage: first_read_export CORPUS OUTPUT NAMESPACES LAYOUT_VERSION");
	run(argv[1], argv[2], unsigned(std::stoul(argv[3])), std::uint32_t(std::stoul(argv[4])));
	return 0;
} catch (const std::exception &e) {
	std::cerr << "first_read_export: " << e.what() << '\n';
	return 1;
}
