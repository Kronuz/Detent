#include "dense_corpus.hh"
#include "paged_packer.hh"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sha256.h>
#include <sstream>
#include <stop_token>
#include <vector>
using namespace dense_test;
namespace x = detent::local::detail;
namespace d = detent::distribution;
static std::size_t read_key(const void *source, std::uint64_t ordinal, std::span<char> out) {
	const auto &key = static_cast<const Rows *>(source)->at(ordinal).key;
	require(key.size() <= out.size());
	std::copy(key.begin(), key.end(), out.begin());
	return key.size();
}
static d::Identity segment_id(std::size_t ordinal) {
	SHA256 hash;
	constexpr std::string_view domain = "model segment";
	hash.add(domain.data(), domain.size());
	std::array<char, 8> bytes{};
	l::write_integer(bytes, 0, ordinal, 8);
	hash.add(bytes.data(), bytes.size());
	p::Digest full;
	hash.getHash(reinterpret_cast<unsigned char *>(full.data()));
	d::Identity result;
	std::copy_n(full.begin(), 16, result.begin());
	return result;
}
static std::string hex(std::span<const char> bytes) {
	std::ostringstream out;
	for (unsigned char c : bytes)
		out << std::hex << std::setfill('0') << std::setw(2) << unsigned(c);
	return out.str();
}
struct Sink {
	struct Page {
		std::vector<char> bytes;
		p::Reference reference;
		std::string path;
	};
	std::vector<Page> pages;
	std::size_t segment = 0, used = 0, bytes = 0;
	p::Limits limits;
	bool drain(x::PagedPacker &packer, l::ValidationBudget &budget) {
		if (!packer.ready())
			return false;
		const auto page = packer.page();
		auto ref = page.summary;
		auto offset = (used + 7) & ~std::size_t(7);
		auto ordinal = segment;
		if (offset + ref.length > p::segment_bytes) {
			++ordinal;
			offset = 0;
		}
		ref.segment = segment_id(ordinal);
		ref.offset = offset;
		p::verify_page({page.bytes->data(), page.bytes->size()}, ref, limits);
		if (!packer.commit(ref, budget))
			return false;
		segment = ordinal;
		used = offset + ref.length;
		bytes += ref.length;
		pages.push_back({{page.bytes->begin(), page.bytes->end()}, ref, std::string(page.path)});
		return true;
	}
};
static void advance(x::PagedPacker &packer, Sink &sink, std::size_t quantum = 8192) {
	std::size_t turns = 0;
	while (!packer.accepted() && !packer.done()) {
		require(++turns < 10000000);
		l::ValidationBudget budget{quantum, 16};
		packer.step(budget);
		sink.drain(packer, budget);
	}
}
static p::Limits limits_for(const Rows &rows) {
	const auto records = std::count_if(rows.begin(), rows.end(),
									   [](const Row &row) { return row.key.front() == 'K'; });
	return corpus_limits(records, rows.size() - records);
}
static Sink run(const std::shared_ptr<Rows> &rows, std::size_t quantum = 8192,
				std::size_t *peak = nullptr) {
	auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	Sink sink;
	{
		const auto limits = sink.limits = limits_for(*rows);
		x::TrieCursor cursor(account, rows, read_key, rows->size(), limits);
		x::PagedPacker packer(account, limits);
		std::size_t turns = 0;
		bool submitted = false, finishing = false;
		while (!packer.done()) {
			require(++turns < 10000000);
			l::ValidationBudget budget{quantum, 16};
			if (!submitted && !finishing) {
				cursor.step(budget);
				if (cursor.ready()) {
					const auto event = cursor.event();
					packer.push(event, event.kind == p::PageKind::Branch
										   ? std::span<const char>{}
										   : std::span<const char>(rows->at(event.ordinal).body));
					submitted = true;
				} else if (cursor.done()) {
					packer.finish();
					finishing = true;
				}
			}
			packer.step(budget);
			sink.drain(packer, budget);
			if (submitted && packer.accepted() && cursor.commit(budget))
				submitted = false;
		}
		if (rows->empty())
			require(packer.root() == p::Reference{});
		else
			require(packer.root() == sink.pages.back().reference);
		if (peak)
			*peak = account->peak();
		l::ValidationBudget budget{SIZE_MAX, SIZE_MAX};
		require(cursor.discard(budget) && packer.discard(budget));
	}
	require(account->used() == 0);
	return sink;
}
static void postorder(const Rows &rows, std::size_t lo, std::size_t hi, std::size_t start,
					  x::PagedPacker &packer, Sink &sink) {
	require(lo < hi && start <= rows[lo].key.size());
	if (hi - lo == 1) {
		const auto &row = rows[lo];
		const auto kind = row.key.front() == 'K' ? p::PageKind::Definition : p::PageKind::Namespace;
		packer.push({kind, lo, 1, row.key, std::string_view(row.key).substr(start), {}}, row.body);
		advance(packer, sink);
		return;
	}
	const auto &first = rows[lo].key, &last = rows[hi - 1].key;
	auto depth = start;
	while (depth < first.size() && depth < last.size() && first[depth] == last[depth])
		++depth;
	require(depth < first.size() && depth < last.size());
	std::vector<std::uint8_t> labels;
	for (auto begin = lo; begin < hi;) {
		const auto label = static_cast<unsigned char>(rows[begin].key[depth]);
		auto end = begin + 1;
		while (end < hi && static_cast<unsigned char>(rows[end].key[depth]) == label)
			++end;
		postorder(rows, begin, end, depth + 1, packer, sink);
		labels.push_back(label);
		begin = end;
	}
	const std::string_view key(first.data(), depth);
	packer.push({p::PageKind::Branch, 0, hi - lo, key, key.substr(start), labels});
	advance(packer, sink);
}
static Sink run_reference(const std::shared_ptr<Rows> &rows, std::size_t *peak = nullptr) {
	auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	Sink sink;
	{
		x::PagedPacker packer(account, sink.limits = limits_for(*rows));
		if (!rows->empty())
			postorder(*rows, 0, rows->size(), 0, packer, sink);
		if (peak)
			*peak = account->peak();
		packer.finish();
		advance(packer, sink);
		l::ValidationBudget release{SIZE_MAX, SIZE_MAX};
		require(packer.discard(release));
	}
	require(account->used() == 0);
	return sink;
}
static void verify_values(const Sink &sink, const Rows &rows) {
	const auto limits = limits_for(rows);
	std::map<std::pair<d::Identity, std::uint32_t>, const Sink::Page *> pages;
	for (const auto &page : sink.pages)
		require(pages.emplace(std::make_pair(page.reference.segment, page.reference.offset), &page)
					.second);
	for (const auto &row : rows) {
		auto ref = sink.pages.back().reference;
		std::size_t consumed = 0, offset = 64;
		const Sink::Page *page = &sink.pages.back();
		for (std::size_t visits = 0;; ++visits) {
			require(visits <= row.key.size());
			const auto h = p::decode_page_header(page->bytes, limits);
			require(h.digest == ref.digest && h.length == ref.length && h.keys == ref.keys &&
					h.height == ref.height);
			const auto node = p::decode_node(page->bytes, offset, limits);
			require(consumed <= row.key.size() && node.prefix.size() <= row.key.size() - consumed &&
					std::string_view(row.key).substr(consumed, node.prefix.size()) == node.prefix);
			consumed += node.prefix.size();
			if (node.kind != p::PageKind::Branch) {
				require(consumed == row.key.size() && std::ranges::equal(node.body, row.body));
				break;
			}
			require(consumed < row.key.size());
			const auto label = static_cast<unsigned char>(row.key[consumed++]);
			std::size_t child = 0;
			while (child < node.labels.size() &&
				   static_cast<unsigned char>(node.labels[child]) != label)
				++child;
			require(child < node.labels.size());
			const auto slot = node.slot(child);
			if (slot & 0x8000) {
				ref = p::decode_external(page->bytes, slot & 0x7fff, limits);
				const auto found = pages.find({ref.segment, ref.offset});
				require(found != pages.end());
				page = found->second;
				offset = 64;
			} else {
				require(slot > offset);
				offset = slot;
			}
		}
	}
}
static void compare_model(const std::string &input, const std::string &directory) {
	std::size_t peak = 0;
	const auto rows = corpus(input);
	bool typed = true;
	for (const auto &row : *rows) {
		try {
			p::decode_key(row.key);
		} catch (const std::invalid_argument &) {
			typed = false;
		}
	}
	const auto sink = typed ? run(rows, 8192, &peak) : run_reference(rows, &peak);
	if (typed) {
		const auto reference = run_reference(rows);
		require(reference.pages.size() == sink.pages.size());
		for (std::size_t i = 0; i < sink.pages.size(); ++i)
			require(sink.pages[i].bytes == reference.pages[i].bytes &&
					sink.pages[i].path == reference.pages[i].path);
	}
	verify_values(sink, *rows);
	std::vector<char> pack;
	std::size_t ordinal = SIZE_MAX;
	for (const auto &page : sink.pages) {
		std::size_t wanted = ordinal == SIZE_MAX ? 0 : ordinal;
		if (page.reference.segment != segment_id(wanted))
			++wanted;
		if (ordinal != wanted) {
			pack = load(directory + "/segment-" + hex(segment_id(wanted)) + ".pack");
			ordinal = wanted;
		}
		require(page.reference.offset <= pack.size() &&
				page.bytes.size() <= pack.size() - page.reference.offset);
		require(
			std::equal(page.bytes.begin(), page.bytes.end(), pack.begin() + page.reference.offset));
	}
	require(peak < 4 * 1024 * 1024);
	std::cout << input << ": " << rows->size() << " records, " << sink.pages.size() << " pages, "
			  << sink.bytes << " page bytes, root " << hex(sink.pages.back().reference.digest)
			  << ", retained peak " << peak << " bytes; exact model pages and placements\n";
}
static void manual(x::PagedPacker &packer, Sink &sink, p::PageKind kind, std::string_view key,
				   std::string_view prefix, std::span<const std::uint8_t> labels,
				   std::uint64_t keys, std::span<const char> body = {}) {
	packer.push({kind, 0, keys, key, prefix, labels}, body);
	advance(packer, sink, 17);
}
static Sink wide(std::size_t body_size) {
	auto account = std::make_shared<d::StorageLedger>(16 * 1024 * 1024);
	x::PagedPacker packer(account);
	Sink sink;
	std::array<std::uint8_t, 256> labels;
	std::vector<char> body(body_size, 'v');
	for (unsigned i = 0; i < 256; ++i) {
		labels[i] = i;
		const std::string key(1, static_cast<char>(i));
		manual(packer, sink, p::PageKind::Definition, key, {}, {}, 1, body);
	}
	manual(packer, sink, p::PageKind::Branch, {}, {}, labels, 256);
	packer.finish();
	advance(packer, sink, 17);
	require(packer.done());
	return sink;
}
static void boundary_checks() {
	auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
	x::PagedPacker packer(account);
	Sink sink;
	std::vector<char> a(1996, 'a'), b(2019, 'b');
	manual(packer, sink, p::PageKind::Definition, "a", {}, {}, 1, a);
	manual(packer, sink, p::PageKind::Definition, "b", {}, {}, 1, b);
	const std::array<std::uint8_t, 2> labels{'a', 'b'};
	manual(packer, sink, p::PageKind::Branch, {}, {}, labels, 2);
	packer.finish();
	advance(packer, sink, 1);
	require(sink.pages.size() == 1 && sink.pages[0].bytes.size() == p::page_capacity);
	const auto original = p::page_commitment(sink.pages[0].bytes, p::Limits{});
	auto changed = sink.pages[0].bytes;
	changed[24] = 2;
	require(p::page_commitment(changed, p::Limits{}) != original);
}
static void injection_checks() {
	auto account = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
	for (auto page : {watermark(), external_branch()}) {
		x::PagedPacker packer(account);
		Sink sink;
		packer.push_cluster(page, {}, {});
		advance(packer, sink, 1);
		packer.finish();
		advance(packer, sink, 1);
		require(sink.pages.size() == 1 && sink.pages[0].bytes == page);
	}
	{
		auto page = external_branch();
		x::PagedPacker packer(account);
		Sink sink;
		const auto branch = p::NodeAt{}.decode(page, p::Limits{});
		packer.push_cluster(page, p::NodeAt::child(branch, branch.slot(1)), "a");
		advance(packer, sink, 1);
		std::vector<char> body(24, 'v');
		manual(packer, sink, p::PageKind::Definition, "b", {}, {}, 1, body);
		const std::array<std::uint8_t, 2> labels{'a', 'b'};
		manual(packer, sink, p::PageKind::Branch, {}, {}, labels, 2);
		packer.finish();
		advance(packer, sink, 1);
		const auto root = p::decode_node(sink.pages[0].bytes, 64, p::Limits{});
		const auto node = p::decode_node(sink.pages[0].bytes, root.slot(0), p::Limits{});
		require(node.kind == p::PageKind::Namespace && node.body.size() == 8 &&
				sink.pages[0].reference.keys == 2);
	}
	require(account->used() == 0);
}
static void resource_checks() {
	auto parent = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
	auto account = std::make_shared<d::StorageLedger>(parent, parent->limit());
	const auto baseline = parent->used();
	std::stop_source stop;
	{
		x::PagedPacker packer(account, {}, stop.get_token());
		std::vector<char> body(100, 'v');
		packer.push({p::PageKind::Definition, 0, 1, "aa", "a", {}}, body);
		l::ValidationBudget zero{0, 0};
		packer.step(zero);
		require(!packer.accepted() && !packer.ready());
		Sink sink;
		advance(packer, sink, 1);
		require(account->used() > 0 && sink.pages.empty());
		stop.request_stop();
		try {
			l::ValidationBudget budget;
			packer.step(budget);
			require(false);
		} catch (const std::system_error &e) {
			require(e.code() == std::errc::operation_canceled);
		}
		require(!packer.ready() && !packer.accepted());
		l::ValidationBudget release{1, 1};
		std::size_t turns = 0;
		while (!packer.discard(release)) {
			release = {1, 1};
			require(++turns < 10000);
		}
	}
	require(account->used() == 0 && parent->used() == baseline);
	{
		std::stop_source token;
		x::PagedPacker packer(account, {}, token.get_token());
		std::vector<char> body(5000, 'v');
		packer.push({p::PageKind::Definition, 0, 1, "a", "a", {}}, body);
		l::ValidationBudget budget{SIZE_MAX, SIZE_MAX};
		packer.step(budget);
		require(packer.ready());
		auto ref = packer.page().summary;
		ref.segment.fill('s');
		token.request_stop();
		try {
			packer.commit(ref, budget);
			require(false);
		} catch (const std::system_error &e) {
			require(e.code() == std::errc::operation_canceled);
		}
		require(!packer.ready());
		require(packer.discard(budget));
	}
	require(account->used() == 0 && parent->used() == baseline);
	{
		x::PagedPacker packer(account);
		Sink sink;
		std::vector<char> body(100, 'v');
		manual(packer, sink, p::PageKind::Definition, "aa", "a", {}, 1, body);
		const auto open = account->used();
		auto pressure = account->admit(account->limit() - open);
		packer.push({p::PageKind::Definition, 1, 1, "ba", "a", {}}, body);
		try {
			l::ValidationBudget budget{SIZE_MAX, SIZE_MAX};
			packer.step(budget);
			require(false);
		} catch (const d::StoragePressure &) {
		}
		require(account->used() == account->limit() && !packer.ready());
		pressure.reset();
		l::ValidationBudget release{SIZE_MAX, SIZE_MAX};
		require(packer.discard(release));
	}
	require(account->used() == 0 && parent->used() == baseline);
}
int main(int argc, char **argv) {
	dense_test::report_uncaught_exceptions();
	if (argc == 3) {
		compare_model(argv[1], argv[2]);
		return 0;
	}
	require(argc == 1);
	auto rows = std::make_shared<Rows>();
	require(run(rows).pages.empty());
	std::array<char, 8> watermark{};
	l::write_integer(watermark, 0, 17, 8);
	rows->push_back({std::string("Wa\0", 3), {watermark.begin(), watermark.end()}});
	auto alone = run(rows, 1);
	require(alone.pages.size() == 1 && alone.bytes == 79 &&
			alone.pages[0].bytes == dense_test::watermark());
	auto root = empty_root();
	root.tree = alone.pages[0].reference;
	root.pages = root.segments = root.namespaces = 1;
	root.bytes = 79;
	require(p::state_commitment(p::decode_root(p::encode_root(root, p::Limits{}), p::Limits{})) == root.tree.digest);
	root = empty_root();
	rows->clear();
	require(run(rows).pages.empty());
	require(p::state_commitment(p::decode_root(p::encode_root(root, p::Limits{}), p::Limits{})) == p::empty_commitment());
	rows->push_back({std::string("Wa\0", 3), {watermark.begin(), watermark.end()}});
	require(run(rows).pages[0].bytes == alone.pages[0].bytes);
	auto large = wide(p::maximum_leaf_body_bytes);
	require(large.pages.size() == 257 && large.pages[0].bytes.size() == p::page_bytes - 12);
	for (unsigned i = 0; i < 256; ++i) {
		require(large.pages[i].reference.digest == large.pages[0].reference.digest);
		require(large.pages[i].path == std::string(1, static_cast<char>(i)));
		if (i)
			require(large.pages[i].reference != large.pages[i - 1].reference);
	}
	auto small = wide(60);
	require(small.pages.size() == 1 && small.pages[0].bytes.size() > p::page_capacity);
	require(p::decode_page_header(small.pages[0].bytes, p::Limits{}).nodes == 257);
	require(wide(64).pages.size() == 1);
	boundary_checks();
	injection_checks();
	resource_checks();
	std::cout << "resumable dense packing: empty/watermark/max-body/256-way/repeated "
				 "occurrences/admission/cancellation passed\n";
}
