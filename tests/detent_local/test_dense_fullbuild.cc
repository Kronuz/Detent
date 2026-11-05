#include "dense_corpus.hh"
#include "detent_distribution/snapshot_encoder.hh"
#include "detent_local/paged_definition.hh"
#include "paged_snapshot_builder.hh"
#include <chrono>
#include <ctime>
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
static double seconds(std::chrono::steady_clock::time_point begin) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
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
static void run(const std::shared_ptr<Rows> &rows, const std::string &model = {}) {
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
	std::uint64_t pack_bytes = 0;
	std::size_t page_count = 0;
	for (const auto &segment : image.segments()) {
		const auto bytes = std::span<const char>(segment.bytes->data(), segment.bytes->size());
		pack_bytes += bytes.size();
		require(p::pack_commitment(bytes) == segment.ownership.checksum);
		std::vector<char> expected;
		if (!model.empty()) {
			// The model uses separate deterministic ids; match segment ordinal and covered page
			// bytes.
			SHA256 id;
			constexpr std::string_view domain = "model segment";
			id.add(domain.data(), domain.size());
			std::array<char, 8> ordinal{};
			l::write_integer(ordinal, 0, &segment - image.segments().data(), 8);
			id.add(ordinal.data(), ordinal.size());
			p::Digest digest;
			id.getHash(reinterpret_cast<unsigned char *>(digest.data()));
			expected =
				load(model + "/segment-" + hex(std::span<const char>(digest).first(16)) + ".pack");
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
			if (!expected.empty())
				require(std::equal(page.begin(), page.end() - p::placement_bytes * h.externals,
								   expected.begin() + at));
			++page_count;
			at = (at + length + 7) & ~std::size_t(7);
		}
	}
	require(page_count == image.root().pages && hashes.page_calls == page_count &&
			hashes.pack_calls == image.segments().size() && hashes.pack_bytes == pack_bytes);
	std::cout << "fullbuild: pages " << image.root().pages << ", pagebytes " << image.root().bytes
			  << ", segments " << image.root().segments << ", packbytes " << pack_bytes << ", root "
			  << hex(p::state_commitment(image.root())) << ", wall_s " << wall << ", cpu_s "
			  << cpu_s << ", page_hash_calls " << hashes.page_calls << ", page_hash_bytes "
			  << hashes.page_bytes << ", pack_hash_calls " << hashes.pack_calls
			  << ", pack_hash_bytes " << hashes.pack_bytes << ", metadata_hash_calls "
			  << hashes.metadata_calls << ", metadata_hash_bytes " << hashes.metadata_bytes
			  << ", output_resident_bytes " << account->used() - baseline << ", peak_bytes "
			  << account->peak() << '\n';
	l::ValidationBudget disposal{SIZE_MAX, SIZE_MAX};
	require(image.discard(disposal));
	require(account->used() == baseline);
}
int main(int argc, char **argv) {
	if (argc == 3) {
		run(corpus(argv[1]), argv[2]);
		return 0;
	}
	require(argc == 1);
	auto rows = std::make_shared<Rows>();
	std::array<char, 8> body{};
	l::write_integer(body, 0, 17, 8);
	rows->push_back({std::string("Wa\0", 3), {body.begin(), body.end()}});
	run(rows);
}
