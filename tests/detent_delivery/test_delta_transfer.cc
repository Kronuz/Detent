// D6's choice of encoding and the delta receiver's D9 outcomes (dense distribution DD4).
//   test_delta_transfer                      hermetic
//   test_delta_transfer CORPUS DIR SPEC...   A2: per scenario, the delta transfer against S8's block
//                                            answer for the same change, and the encoding D6 picks
#include "delta_scenarios.hh"
#include "delta_transfer.hh"
#include "detent_distribution/session_frames.hh"
#include "page_window.hh"
#include "paged_page_access.hh"
#include <random>

using namespace delta_test;
namespace v = detent::delivery;
namespace r = detent::relay;

static std::vector<char> noise(std::size_t size, unsigned seed) {
	std::mt19937 random(seed);
	std::vector<char> out(size);
	for (auto &c : out)
		c = static_cast<char>(random());
	return out;
}

static void choices() {
	require(v::sent_bytes({}) == 0);
	const std::vector<char> zeros(100000);
	const auto random = noise(d::fragment_bytes + 1, 1);
	require(v::sent_bytes(zeros) > 2 * d::frame_header_bytes && v::sent_bytes(zeros) < 1000);
	require(v::sent_bytes(random) > random.size() + 2 * d::frame_header_bytes);
	// Rule 1: more than three times the raw records goes as records, uncompressed.
	auto choice = v::choose_encoding(std::vector<char>(3001), std::vector<char>(1000));
	require(choice.encoding == v::Encoding::Records && choice.shortcut && !choice.pages_sent && !choice.records_sent);
	// Rule 2: within three times, the smaller as sent.
	choice = v::choose_encoding(zeros, noise(40000, 2));
	require(choice.encoding == v::Encoding::Pages && !choice.shortcut && choice.pages_sent < choice.records_sent);
	choice = v::choose_encoding(noise(3000, 3), zeros);
	require(choice.encoding == v::Encoding::Records && !choice.shortcut && choice.pages_sent > choice.records_sent);
	const auto same = noise(5000, 4);
	choice = v::choose_encoding(same, same);
	require(choice.encoding == v::Encoding::Records && choice.pages_sent == choice.records_sent);
	std::cout << "choices: ok\n";
}

// A base whose pages fail first touch from the `fail`-th read on.
static x::TransferPages failing(x::TransferPages pages, std::size_t fail) {
	auto reads = std::make_shared<std::size_t>(0);
	return [pages = std::move(pages), reads, fail](const p::Reference &reference) {
		if ((*reads)++ >= fail)
			throw x::ReadSourceFailure("local page failed first-touch verification");
		return pages(reference);
	};
}

static void receiver() {
	const auto rows = synthetic(30000);
	const Built base(rows, 'b'), target(changed(*rows, 5, 29, 900), 't'), other(synthetic(20000, 991), 'o');
	const auto diff = r::merkle_diff(target.root(), target.pages(), base.root(), base.pages(), target.limits());
	const auto stream = x::make_transfer(diff.pages, target.pages(), target.limits());
	require(stream == wire_form(diff.pages, target.pages(), target.limits()));
	auto account = std::make_shared<d::StorageLedger>(1ull << 32);
	const auto id = [](std::uint32_t number) {
		l::SegmentId segment{};
		segment.fill('r');
		l::write_integer(segment, 0, number, 4);
		return segment;
	};
	const x::TransferBase held{base.root(), base.pages()};
	auto receipt = v::receive_delta(account, stream, held, target.root().digest, id, target.limits());
	require(receipt.outcome == v::DeltaOutcome::Published && receipt.received &&
			receipt.received->tree.digest == target.root().digest && !receipt.refusal);
	// A refusal asks one other source, then takes records; nothing is published either way.
	auto wrong = target.root().digest;
	wrong[0] ^= 1;
	receipt = v::receive_delta(account, stream, held, wrong, id, target.limits());
	require(receipt.outcome == v::DeltaOutcome::OtherSource && !receipt.received &&
			receipt.refusal == x::TransferRefusal::RootDigest);
	receipt = v::receive_delta(account, stream, held, wrong, id, target.limits(), 1);
	require(receipt.outcome == v::DeltaOutcome::Records && !receipt.received);
	// A damaged base takes the full copy: a base page failing first touch, or a base lacking a page.
	receipt = v::receive_delta(account, stream, {base.root(), failing(base.pages(), 0)}, target.root().digest, id,
							   target.limits());
	require(receipt.outcome == v::DeltaOutcome::FullCopy && !receipt.received && !receipt.refusal);
	receipt = v::receive_delta(account, stream, {other.root(), other.pages()}, target.root().digest, id,
							   target.limits(), 1);
	require(!receipt.received && receipt.refusal &&
			(receipt.outcome == v::DeltaOutcome::FullCopy) == (receipt.refusal == x::TransferRefusal::BaseLacks));
	std::cout << "receiver: ok (other base refused: " << receipt.reason << ")\n";
}

// S8's block answer: one namespace's history with its base at R and one block (R, H] holding each
// key the change wrote, at its last write.
class BlockHistory final : public d::NamespaceHistory {
  public:
	struct Write {
		std::string key;
		std::uint64_t revision;
		std::optional<detent::KnobDef> value;
	};
	BlockHistory(d::NamespaceCursor head, std::uint64_t base, std::vector<Write> writes)
		: head_(head), base_(base), span_{base + 1, head.position, true}, writes_(std::move(writes)) {}
	d::NamespaceCursor head() const noexcept override { return head_; }
	std::uint64_t cut() const noexcept override { return base_; }
	std::uint64_t ring_start() const noexcept override { return head_.position; }
	std::span<const d::HistorySpan> spans() const noexcept override { return {&span_, 1}; }
	std::unique_ptr<d::HistoryReader> read_span(std::size_t) const override {
		return std::make_unique<Reader>(writes_);
	}
	std::unique_ptr<d::HistoryReader> read_ring(std::uint64_t) const override {
		return std::make_unique<Reader>(none_);
	}
	std::unique_ptr<d::HistoryReader> read_full_copy() const override {
		throw std::logic_error("a block answer reads no full copy");
	}

  private:
	class Reader final : public d::HistoryReader {
	  public:
		explicit Reader(const std::vector<Write> &writes) : writes_(writes) {}
		const d::HistoryRecord *next() override {
			if (at_ == writes_.size())
				return nullptr;
			const auto &write = writes_[at_++];
			record_ = {write.key, write.revision, write.value ? &*write.value : nullptr};
			return &record_;
		}

	  private:
		const std::vector<Write> &writes_;
		std::size_t at_ = 0;
		d::HistoryRecord record_;
	};
	d::NamespaceCursor head_;
	std::uint64_t base_;
	d::HistorySpan span_;
	std::vector<Write> writes_, none_;
};
static detent::KnobDef knob(std::span<const char> body) {
	const auto definition = p::validate_definition(body, UINT64_MAX);
	detent::KnobDef out{definition.default_value};
	out.rollout_pct = definition.rollout;
	out.rollout_value = definition.rollout_value;
	auto at = p::header_size(definition);
	for (std::uint32_t i = 0; i < definition.overrides; ++i) {
		const auto row = p::read_override(body, at);
		out.overrides.emplace(std::string(row.key), row.value);
	}
	return out;
}
static std::map<std::string, std::uint64_t> watermarks(const Rows &rows) {
	std::map<std::string, std::uint64_t> out;
	for (const auto &row : rows)
		if (row.key.front() == 'W')
			out.emplace(std::string(p::decode_key(row.key).name_space), l::read_integer(row.body, 0, 8));
	return out;
}
// The W6 body a relay sends a store at the base for the change from `base` to `target`: one frame
// per namespace the change advanced, each the block of its changed keys and tombstones.
static std::string block_answer(const Rows &base, const Rows &target) {
	const auto from = watermarks(base), to = watermarks(target);
	std::map<std::string, std::vector<BlockHistory::Write>> writes;
	auto write = [&](const Row &row, bool removed) {
		const auto key = p::decode_key(row.key);
		const std::string name(key.name_space);
		const auto r = from.at(name), h = to.at(name);
		auto revision = removed ? h : l::read_integer(row.body, 0, 8);
		if (revision <= r || revision > h)
			revision = h; // a modeled insert keeps its source's revision; its write is at h
		writes[name].push_back({std::string(key.local_name), revision,
								removed ? std::nullopt : std::optional(knob(row.body))});
	};
	std::size_t i = 0, j = 0;
	while (i < base.size() || j < target.size()) {
		const auto *a = i < base.size() && base[i].key.front() == 'K' ? &base[i] : nullptr;
		const auto *b = j < target.size() && target[j].key.front() == 'K' ? &target[j] : nullptr;
		if (!a && !b)
			break;
		if (b && (!a || b->key < a->key))
			write(*b, false), ++j;
		else if (a && (!b || a->key < b->key))
			write(*a, true), ++i;
		else {
			if (a->body != b->body)
				write(*b, false);
			++i, ++j;
		}
	}
	std::vector<std::unique_ptr<BlockHistory>> histories;
	std::vector<d::NamespaceRequest> requests;
	for (const auto &[name, position] : to) {
		if (position == from.at(name))
			continue;
		detent::Identity cluster{}, incarnation{};
		cluster.fill('c');
		incarnation.fill('i');
		histories.push_back(std::make_unique<BlockHistory>(d::NamespaceCursor{cluster, incarnation, position},
														   from.at(name), std::move(writes[name])));
		requests.push_back({name, histories.back().get(), d::NamespaceCursor{cluster, incarnation, from.at(name)}});
		require(d::select_answer(*histories.back(), requests.back().store).kind == d::AnswerKind::Blocks);
	}
	return d::encode_session_frames(std::nullopt, requests);
}

// A2: per scenario, pages against S8's block answer and D6's choice.
static void scenarios(const std::string &corpus_path, const std::string &dir, std::span<char *> specs) {
	const auto rows = corpus(corpus_path);
	const Built base(rows, 'b');
	std::cout << "spec, pages, pages raw, records raw, raw ratio, pages sent, records sent, sent ratio, choice\n";
	for (const std::string spec : specs) {
		const auto after = apply(*rows, dir + "/" + spec + ".patch");
		const Built target(after, 't');
		const auto diff = r::merkle_diff(target.root(), target.pages(), base.root(), base.pages(), target.limits());
		const auto pages = x::make_transfer(diff.pages, target.pages(), target.limits());
		const auto records = block_answer(*rows, *after);
		const auto choice = v::choose_encoding(pages, records);
		const auto pages_sent = v::sent_bytes(pages), records_sent = v::sent_bytes(records);
		const auto raw = double(pages.size()) / double(records.size()),
				   sent = double(pages_sent) / double(records_sent);
		std::cout << spec << ", " << diff.pages.size() << ", " << pages.size() << ", " << records.size() << ", "
				  << raw << ", " << pages_sent << ", " << records_sent << ", " << sent << ", "
				  << (choice.encoding == v::Encoding::Pages ? "pages" : "records")
				  << (choice.shortcut ? " (rule 1)" : " (rule 2)") << std::endl;
		// Rule 1's shortcut never sends records where pages were smaller as sent.
		require(!choice.shortcut || pages_sent > records_sent);
		require(choice.shortcut || choice.encoding == (pages_sent < records_sent ? v::Encoding::Pages
																				  : v::Encoding::Records));
		if (spec.starts_with("u:"))
			require(choice.encoding == v::Encoding::Records);
		if (spec == "c:1000000")
			require(choice.encoding == v::Encoding::Pages);
	}
}

int main(int argc, char **argv) {
	report_uncaught_exceptions();
	if (argc == 1) {
		choices();
		receiver();
	} else {
		require(argc >= 4);
		scenarios(argv[1], argv[2], std::span(argv + 3, argc - 3));
	}
}
