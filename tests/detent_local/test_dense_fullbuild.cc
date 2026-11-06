#include "dense_corpus.hh"
#include "change_file.hh"
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/paged_definition.hh"
#include "paged_snapshot_builder.hh"
#include "paged_read_state.hh"
#include "segment_names.hh"
#include "trusted_records.hh"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sha256.h>
#include <sstream>
namespace d = detent::distribution;
namespace x = detent::local::detail;
using namespace dense_test;
class CorpusSource final : public d::SnapshotSource {
  public:
	CorpusSource(std::shared_ptr<Rows> rows, std::shared_ptr<d::StorageLedger> account)
		: rows_(std::move(rows)), account_(std::move(account)), cursor_(empty_root().cursor) {
		cursor_.revision = 0;
		for (const auto &row : *rows_)
			if (row.key.front() == 'W') {
				const auto key = p::decode_key(row.key);
				const auto revision = l::read_integer(row.body, 0, 8);
				namespaces_.push_back({key.name_space, revision});
				cursor_.revision = std::max(cursor_.revision, revision);
			}
		for (std::size_t i = 0; i < namespaces_.size(); ++i)
			indices_.emplace(namespaces_[i].name, i);
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
		current_body_ = row.body;
		override_at_ = p::definition_header_bytes;
		remaining_ = def.overrides;
		current_name_ = std::string(key.name_space) + ":" + std::string(key.local_name);
		d::WireDefinition definition = detent::IntDef{def.default_int};
		if (def.boolean)
			definition = d::WireBoolDef{def.default_bool, {0, def.overrides}, def.rollout};
		return {
			d::Record{static_cast<std::uint32_t>(index), current_name_, def.revision, definition},
			false};
	}
	std::optional<d::OverrideRow> next_override() override {
		if (!remaining_)
			return {};
		--remaining_;
		const auto value = p::read_override(current_body_, override_at_);
		detent::GroupOverride result = value.value != 0;
		if (value.percentage)
			result = detent::GroupRollout{value.value};
		return d::OverrideRow{value.key, result};
	}

  private:
	std::shared_ptr<Rows> rows_;
	std::shared_ptr<d::StorageLedger> account_;
	d::Cursor cursor_;
	std::vector<d::NamespaceRevision> namespaces_;
	std::map<std::string_view, std::size_t> indices_;
	std::string current_name_;
	std::span<const char> current_body_;
	std::size_t ordinal_ = 0, override_at_ = 0;
	std::uint32_t remaining_ = 0;
};
static std::string hex(std::span<const char> bytes) {
	std::ostringstream out;
	for (unsigned char c : bytes)
		out << std::hex << std::setfill('0') << std::setw(2) << unsigned(c);
	return out.str();
}
static l::StorageIdentity model_segment(std::size_t index) {
	SHA256 hash;
	constexpr std::string_view domain = "model segment";
	hash.add(domain.data(), domain.size());
	std::array<char, 8> ordinal{};
	l::write_integer(ordinal, 0, index, 8);
	hash.add(ordinal.data(), ordinal.size());
	p::Digest digest;
	hash.getHash(reinterpret_cast<unsigned char *>(digest.data()));
	l::StorageIdentity result;
	std::copy_n(digest.begin(), result.size(), result.begin());
	return result;
}
static double seconds(std::chrono::steady_clock::time_point begin) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}
static void reshape(const std::shared_ptr<Rows> &rows, unsigned namespaces) {
	require(namespaces == 1 || namespaces == 1000);
	std::uint64_t revision = 0;
	for (const auto &row : *rows)
		if (row.key.front() == 'W')
			revision = std::max(revision, l::read_integer(row.body, 0, 8));
	std::erase_if(*rows, [](const auto &row) { return row.key.front() == 'W'; });
	for (auto &row : *rows) {
		const auto local = std::string(p::decode_key(row.key).local_name);
		row.key = std::string("Kprimary\0", 9) + local + '\0';
	}
	for (unsigned i = 0; i < namespaces; ++i) {
		const auto name = i == 0 ? "primary" : "side" + std::to_string(i);
		std::vector<char> watermark(8);
		l::write_integer(watermark, 0, revision, 8);
		rows->push_back({"W" + name + '\0', std::move(watermark)});
		if (i)
			for (unsigned j = 0; j < 100; ++j) {
				std::vector<char> body(p::definition_header_bytes);
				p::encode_definition(body, detent::IntDef{17}, revision);
				rows->push_back({"K" + name + '\0' + "key" + std::to_string(j) + '\0',
								 std::move(body)});
			}
	}
	std::sort(rows->begin(), rows->end(), [](const auto &a, const auto &b) { return a.key < b.key; });
}
class CountingAccess final : public x::PagedPageAccess {
  public:
	explicit CountingAccess(const x::PagedImage &image) : image_(image) {}
	std::span<const char> resolve(const p::Reference &ref) const override {
		if (std::find(seen.begin(), seen.end(), ref) == seen.end())
			seen.push_back(ref);
		for (const auto &segment : image_.segments())
			if (segment.ownership.segment == ref.segment)
				return std::span(segment.bytes->data(), segment.bytes->size())
					.subspan(ref.offset, ref.length);
		throw std::runtime_error("lookup names an absent segment");
	}
	std::uint32_t maximum_overrides() const noexcept override { return 256; }
	bool available() const noexcept override { return true; }
	mutable std::vector<p::Reference> seen;
  private:
	const x::PagedImage &image_;
};
static void lookup_pages(const Rows &rows, const x::PagedImage &image) {
	auto access = std::make_shared<CountingAccess>(image);
	x::PagedReadState state{image.root(), {}, access};
	std::map<std::size_t, std::size_t> histogram;
	std::size_t definitions = 0;
	for (const auto &row : rows) {
		if (row.key.front() == 'W')
			break;
		const auto key = p::decode_key(row.key);
		access->seen.clear();
		require(state.definition(key.name_space, key.local_name).has_value());
		++histogram[access->seen.size()];
		++definitions;
	}
	std::cout << "lookup_pages: definitions " << definitions;
	for (const auto &[pages, count] : histogram)
		std::cout << ", pages_" << pages << ' ' << count;
	std::cout << '\n';
}
static std::shared_ptr<const d::ValidatedSnapshot>
admit(const std::shared_ptr<Rows> &rows, const std::shared_ptr<d::StorageLedger> &account) {
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
static void run(const std::shared_ptr<Rows> &rows, const std::string &model = {},
				const std::filesystem::path &output = {}) {
	auto account = std::make_shared<d::StorageLedger>(4ull * 1024 * 1024 * 1024);
	auto start = std::chrono::steady_clock::now();
	auto source = admit(rows, account);
	std::cout << "admission: records " << source->records().size() << ", namespaces "
			  << source->namespaces().size() << ", wall_s " << seconds(start) << ", resident_bytes "
			  << account->used() << '\n';
	const auto baseline = account->used();
	p::Limits limits;
	limits.records = source->records().size();
	limits.namespaces = source->namespaces().size();
	limits.pages = rows->size() * 2;
	limits.bytes = 4ull * 1024 * 1024 * 1024;
	l::StorageIdentity storage{}, generation{};
	storage.fill('s');
	generation.fill('g');
	p::reset_hash_counts();
	start = std::chrono::steady_clock::now();
	const auto cpu = std::clock();
	x::PagedSnapshotBuilder builder(source, storage, generation, limits);
	std::size_t turns = 0;
	while (!builder.done()) {
		l::ValidationBudget budget{65536, 256};
		builder.step(budget);
		require(++turns < 100000000);
	}
	const auto wall = seconds(start), cpu_s = double(std::clock() - cpu) / CLOCKS_PER_SEC;
	const auto hashes = p::hash_counts();
	auto image = builder.take();
	std::uint64_t pack_bytes = 0, page_bytes = 0, page_hash_bytes = 0;
	std::map<l::StorageIdentity, l::StorageIdentity> model_ids;
	for (std::size_t i = 0; i < image.segments().size(); ++i)
		model_ids.emplace(model_segment(i), image.segments()[i].ownership.segment);
	if (!model.empty()) {
		std::size_t model_packs = 0;
		for (const auto &entry : std::filesystem::directory_iterator(model))
			if (entry.path().extension() == ".pack")
				++model_packs;
		require(model_packs == image.segments().size());
	}
	p::Reference last_page;
	std::size_t page_count = 0;
	for (const auto &segment : image.segments()) {
		const auto bytes = std::span<const char>(segment.bytes->data(), segment.bytes->size());
		pack_bytes += bytes.size();
		require(p::pack_commitment(bytes) == segment.ownership.checksum);
		std::vector<char> expected;
		if (!model.empty()) {
			// Only segment identities differ; every placement keeps its seal-order
			// offset.
			expected = load(model + "/segment-" +
							hex(model_segment(&segment - image.segments().data())) + ".pack");
			require(expected.size() == bytes.size());
		}
		for (std::size_t at = 0; at < bytes.size();) {
			const auto length = l::read_integer(bytes, at + 8, 4);
			const auto page = bytes.subspan(at, length);
			const auto h = p::decode_page_header(page, limits);
			p::Reference ref{segment.ownership.segment,
							 static_cast<std::uint32_t>(at),
							 h.length,
							 h.keys,
							 h.height,
							 h.digest};
			p::verify_page(page, ref, limits);
			if (!expected.empty()) {
				const auto expected_page = std::span<const char>(expected).subspan(at, length);
				require(std::equal(page.begin(), page.end() - p::placement_bytes * h.externals,
								   expected_page.begin()));
				for (std::uint16_t i = 0; i < h.externals; ++i) {
					auto wanted = p::decode_external(expected_page, i, limits);
					wanted.segment = model_ids.at(wanted.segment);
					require(p::decode_external(page, i, limits) == wanted);
				}
			}
			last_page = ref;
			page_bytes += length;
			page_hash_bytes += length - 32 - p::placement_bytes * h.externals;
			++page_count;
			const auto end = at + length;
			at = (end + 7) & ~std::size_t(7);
			for (auto padding = end; padding < std::min(at, bytes.size()); ++padding)
				require(bytes[padding] == 0);
		}
	}
	require(page_count == image.root().pages && page_bytes == image.root().bytes &&
			image.root().tree == last_page && image.root().segments == image.segments().size());
	require(hashes == p::HashCounts{page_count, page_hash_bytes, image.segments().size(),
									pack_bytes, image.segments().size() + 1,
									72 * image.segments().size() + p::root_bytes});
	auto root_bytes = image.bytes();
	require(p::decode_root({root_bytes->data(), root_bytes->size()}, limits).tree == last_page);
	require(p::metadata_commitment({root_bytes->data(), root_bytes->size()}) ==
			image.descriptor().checksum);
	std::cout << "fullbuild: pages " << image.root().pages << ", pagebytes " << image.root().bytes
			  << ", segments " << image.root().segments << ", packbytes " << pack_bytes << ", root "
			  << hex(p::state_commitment(image.root())) << ", wall_s " << wall << ", cpu_s "
			  << cpu_s << ", page_hash_calls " << hashes.page_calls << ", page_hash_bytes "
			  << hashes.page_bytes << ", pack_hash_calls " << hashes.pack_calls
			  << ", pack_hash_bytes " << hashes.pack_bytes << ", metadata_hash_calls "
			  << hashes.metadata_calls << ", metadata_hash_bytes " << hashes.metadata_bytes
			  << ", output_resident_bytes " << account->used() - baseline << ", peak_bytes "
			  << account->peak() << '\n';
	if (!output.empty()) {
		lookup_pages(*rows, image);
		require(std::filesystem::create_directories(output));
		std::filesystem::permissions(output, std::filesystem::perms::owner_all);
		trusted_records::put(output / "storage.meta", l::encode_storage_identity(storage));
		const auto name = l::generation_name(generation);
		trusted_records::put(output / (name + ".image"), root_bytes->view());
		trusted_records::put(output / (name + ".lease"), l::encode_generation_ownership(
			{storage, generation, image.descriptor().length, image.descriptor().checksum,
			 p::format_version}));
		const auto descriptor = l::encode_descriptor(image.descriptor());
		trusted_records::put(output / (name + ".descriptor"), descriptor);
		for (const auto &segment : image.segments()) {
			const x::SegmentNames names(segment.ownership.segment);
			trusted_records::put(output / names.payload_name(), segment.bytes->view());
			trusted_records::put(output / names.owner_name(),
							 l::encode_segment_ownership(segment.ownership));
		}
		trusted_records::put(output / "selected", descriptor);
		const int directory = ::open(output.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		require(directory >= 0);
		{
			x::ChangeWriter change(directory, 1, ::getuid(), 0600);
			change.bump_all();
		}
		require(::close(directory) == 0);
		for (const auto &row : *rows) {
			if (row.key.front() == 'W')
				break;
			const auto definition = p::validate_definition(row.body, source->cursor().revision);
			if (!definition.boolean) {
				const auto key = p::decode_key(row.key);
				std::cout << "intprobe: key " << key.name_space << ':' << key.local_name
						  << ", expected " << definition.default_int << '\n';
				break;
			}
		}
		std::cout << "export: directory " << output << '\n';
	}
	root_bytes.reset();
	l::ValidationBudget disposal{SIZE_MAX, SIZE_MAX};
	require(image.discard(disposal));
	require(account->used() == baseline);
}
int main(int argc, char **argv) {
	if (argc >= 3 && argc <= 5) {
		auto rows = corpus(argv[1]);
		if (argc == 5)
			reshape(rows, std::stoul(argv[4]));
		run(rows, argv[2], argc >= 4 ? argv[3] : "");
		return 0;
	}
	require(argc == 1);
	auto rows = std::make_shared<Rows>();
	run(rows);
	std::array<char, 8> body{};
	l::write_integer(body, 0, 17, 8);
	rows->push_back({std::string("Wa\0", 3), {body.begin(), body.end()}});
	run(rows);
}
