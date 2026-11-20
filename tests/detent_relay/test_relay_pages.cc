// The relay's page answers (dense distribution DD3, D6, D7) over a scripted upstream that serves real
// history files. A reader that takes pages gets the full copy as a pages frame, and DD1's receiver
// rebuilds from it the relay's build: every pack byte for byte under the agent's derived ids, and its
// root. The relay builds once per head. A base its page window holds gets D6's smaller encoding. A
// position that keeps records gets the same bytes as from a relay that never built. The mirror
// refuses a pages frame from its upstream.
#include "delta_transfer.hh"
#include "detent_local/derived_ids.hh"
#include "detent_relay/namespace_relay.hh"
#include "history_format.hh"
#include "page_copy.hh"
#include <cstdio>
#include <filesystem>
#include <map>
#include <source_location>
#include <sys/stat.h>

namespace d = detent::distribution;
namespace r = detent::relay;
namespace h = detent::authority::history;
namespace l = detent::local;
namespace p = detent::local::paged;
namespace x = detent::local::detail;
namespace c = kronuz::io::completion;

namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("check failed at line " + std::to_string(where.line()));
}
d::Identity identity(char fill) {
	d::Identity out;
	out.fill(fill);
	return out;
}
const d::Identity cluster = identity('C');

// Per namespace: a base at the cut and level-0 sections above it, each a real encoded file.
class Upstream final : public r::Upstream {
  public:
	std::uint64_t seal = 1, frame_calls = 0;
	bool send_pages = false;
	// A base at `keys` positions: key i written at position i + 1.
	void create(const std::string &name, std::size_t keys) {
		auto &n = namespaces_[name];
		n.base = {name, identity('i'), 0, keys, {}};
		for (std::size_t i = 0; i < keys; ++i)
			n.base.changes.push_back(change(name, i, i + 1, int(i)));
		n.files.clear();
		n.files.push_back(add(r::MirrorFile::Kind::Base, "base-" + name + "-" + std::to_string(keys), 0, keys,
							  h::encode_base(n.base)));
		++seal;
	}
	// One seal: each key in `keys` (ascending) written once, at consecutive positions, to `salt + i`.
	void write(const std::string &name, const std::vector<std::size_t> &keys, int salt) {
		auto &n = namespaces_.at(name);
		const auto first = n.files.back().last + 1;
		h::Block block{name, identity('i'), first, first + keys.size() - 1, {}};
		for (std::size_t j = 0; j < keys.size(); ++j)
			block.changes.push_back(change(name, keys[j], first + j, salt + int(keys[j])));
		n.files.push_back(add(r::MirrorFile::Kind::Section, "seal-" + std::to_string(seal) + "#" + name, block.first,
							  block.last, h::encode_block(block)));
		++seal;
	}
	r::MirrorTable layouts(std::span<const std::string> names) override {
		r::MirrorTable out{0, seal, {}};
		for (const auto &name : names)
			if (const auto found = namespaces_.find(name); found != namespaces_.end())
				out.entries.push_back({name, identity('i'), seal, seal, found->second.base.last, "policy-1",
									   found->second.files});
		return out;
	}
	std::string fetch(std::string_view, const r::MirrorFile &file) override { return bytes_.at(file.name); }
	// No ring entries; or, to a relay, a pages frame it must refuse.
	std::string frames(std::span<const r::Position> positions) override {
		++frame_calls;
		if (!send_pages)
			return d::encode_session_frames(std::nullopt, {});
		const auto &at = *positions.front().cursor;
		static const std::string transfer(100, 'p');
		d::PagesAnswer pages{{}, at.position, {at.cluster, at.incarnation, at.position + 1}, transfer};
		pages.root.fill('\x07');
		const d::NamespaceRequest request{positions.front().name, nullptr, {}, {}, &pages};
		return d::encode_session_frames(std::nullopt, {&request, 1});
	}

  private:
	struct Namespace {
		h::Block base;
		std::vector<r::MirrorFile> files;
	};
	static h::Change change(const std::string &name, std::size_t key, std::uint64_t revision, int value) {
		char text[16];
		std::snprintf(text, sizeof text, "k%06zu", key);
		detent::KnobDef definition{std::int64_t(value)};
		if (key % 7 == 3)
			definition.overrides.emplace("group-" + std::to_string(key % 5), std::int64_t(value * 3));
		return {name + ":" + text, revision, detent::authority::encode_definition(definition)};
	}
	r::MirrorFile add(r::MirrorFile::Kind kind, std::string name, std::uint64_t first, std::uint64_t last,
					  std::string bytes) {
		r::MirrorFile file{kind, name, first, last, bytes.size(), r::digest_of(bytes)};
		bytes_[std::move(name)] = std::move(bytes);
		return file;
	}
	std::map<std::string, Namespace> namespaces_;
	std::map<std::string, std::string> bytes_;
};

struct Frame {
	std::string name;
	d::FrameKind kind{};
	std::uint64_t after = 0, records = 0;
	d::NamespaceCursor to;
	d::RootDigest root{};
	std::string transfer;
};
struct Collect final : d::FrameSink {
	std::vector<Frame> frames;
	void frame(std::string_view name, d::FrameKind kind, std::uint64_t after, const d::NamespaceCursor &to) override {
		frames.push_back({std::string(name), kind, after, 0, to, {}, {}});
	}
	void piece(d::PieceKind, std::uint64_t, std::uint64_t) override {}
	void record(std::string_view, std::uint64_t, const detent::KnobDef *) override { ++frames.back().records; }
	void pages(const d::RootDigest &root) override { frames.back().root = root; }
	void chunk(std::string_view bytes) override { frames.back().transfer.append(bytes); }
};
std::vector<Frame> decode(std::string_view body) {
	Collect sink;
	d::Limits limits;
	limits.bytes = std::uint64_t{1} << 32;
	d::SessionFramesDecoder decoder(sink, limits);
	std::span<const char> input(body);
	auto budget = body.size();
	decoder.feed(input, budget);
	decoder.finish();
	return sink.frames;
}
std::string answer(r::NamespaceRelay &relay, std::vector<r::Position> positions) {
	return relay.frames(positions);
}

// What an agent does with a pages frame: DD1's receiver, under the ids it names a full copy's build by.
struct Received {
	x::ReceivedTransfer transfer;
	std::map<l::SegmentId, std::string_view> segments;
	x::TransferBase base() const {
		return {transfer.tree, [this](const p::Reference &reference) -> std::span<const char> {
					const auto bytes = segments.at(reference.segment);
					return {bytes.data() + reference.offset, reference.length};
				}};
	}
};
std::unique_ptr<Received> receive(const Frame &frame, const r::BuiltCopy &target, const Received *base = nullptr) {
	check(frame.kind == d::FrameKind::Pages);
	const auto id = l::next_generation(std::nullopt, frame.to, d::Profile{}, true).id;
	p::Digest root;
	std::copy(frame.root.begin(), frame.root.end(), root.begin());
	const auto base_tree = base ? std::optional(base->base()) : std::nullopt;
	auto out = std::make_unique<Received>(Received{
		x::receive_transfer(std::make_shared<d::StorageLedger>(std::size_t{1} << 32), frame.transfer,
							base_tree ? &*base_tree : nullptr, {root, frame.to.position},
							[id](std::uint32_t n) { return l::derive_segment(id, n - 1); }, target.limits()),
		{}});
	for (const auto &segment : out->transfer.segments)
		out->segments.emplace(segment.ownership.segment, segment.bytes->view());
	if (base)
		for (const auto &[id_of, bytes] : base->segments)
			out->segments.emplace(id_of, bytes);
	return out;
}
// The build a records reader's answer carries, as the relay builds it.
std::shared_ptr<const r::BuiltCopy> reference_build(r::NamespaceRelay &relay, const std::string &name) {
	return r::build_copy(answer(relay, {{name, std::nullopt, false}}));
}

void prepare(const std::filesystem::path &root) {
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	check(::chmod(root.c_str(), 0700) == 0);
}

// A1: the full copy as pages, built once, reproduces the relay's build; records stay records.
void full_copy(const std::filesystem::path &scratch) {
	prepare(scratch / "pages");
	prepare(scratch / "plain");
	Upstream up;
	up.create("app", 3000);
	up.create("beta", 40);
	const std::vector<std::string> names{"app", "beta"};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "pages", 0644), cluster, up);
	r::NamespaceRelay plain(std::make_shared<c::PosixIO>(scratch / "plain", 0644), cluster, up);
	relay.sync(names);
	plain.sync(names);
	const std::vector<r::Position> records{{"app", std::nullopt, false}, {"beta", std::nullopt, false}};
	const auto before = answer(relay, records);

	const auto body = answer(relay, {{"app", std::nullopt, true}, {"beta", std::nullopt, false}});
	const auto frames = decode(body);
	check(frames.size() == 2 && frames[0].name == "app" && frames[0].kind == d::FrameKind::Pages &&
		  frames[0].after == 0 && frames[0].to == *relay.head("app") && frames[0].records == 0);
	check(frames[1].name == "beta" && frames[1].kind == d::FrameKind::FullCopy && frames[1].records == 40);
	check(relay.pages().builds == 1 && relay.pages().full_copies == 1);

	const auto built = reference_build(relay, "app");
	check(built->at() == frames[0].to && built->root() == frames[0].root);
	check(std::string_view(built->transfer().data(), built->transfer().size()) == frames[0].transfer);
	const auto got = receive(frames[0], *built);
	const auto packs = built->segments();
	check(got->transfer.tree == built->tree() && got->transfer.segments.size() == packs.size());
	for (std::size_t i = 0; i < packs.size(); ++i)
		check(got->transfer.segments[i].ownership.segment == packs[i].ownership.segment &&
			  got->transfer.segments[i].bytes->view() == packs[i].bytes->view());

	// Another cold reader at the same head reuses the build; records readers see no change.
	check(answer(relay, {{"app", std::nullopt, true}}) == answer(relay, {{"app", std::nullopt, true}}));
	check(relay.pages().builds == 1 && relay.pages().full_copies == 3);
	check(answer(relay, records) == before && answer(plain, records) == before);
	check(plain.pages().builds == 0);
	std::fprintf(stderr, "full copy: %zu pages, %zu packs, %zu transfer bytes, root equal, packs byte for byte\n",
				 std::size_t(got->transfer.pages), packs.size(), frames[0].transfer.size());
}

// D6 over the page window: a base the window holds gets pages only when smaller as sent.
void deltas(const std::filesystem::path &scratch) {
	prepare(scratch / "deltas");
	Upstream up;
	up.create("app", 3000);
	const std::vector<std::string> names{"app"};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "deltas", 0644), cluster, up);
	std::vector<std::shared_ptr<const r::BuiltCopy>> builds;
	std::vector<std::unique_ptr<Received>> held;
	auto cold = [&] {
		relay.sync(names);
		const auto frames = decode(answer(relay, {{"app", std::nullopt, true}}));
		check(frames.size() == 1);
		builds.push_back(reference_build(relay, "app"));
		held.push_back(receive(frames[0], *builds.back()));
		check(held.back()->transfer.tree == builds.back()->tree());
	};
	cold();
	struct Step {
		const char *name;
		std::vector<std::size_t> keys;
	};
	std::vector<std::size_t> scattered, clustered;
	for (std::size_t i = 0; i < 3000; i += 600)
		scattered.push_back(i);
	for (std::size_t i = 0; i < 3000; ++i)
		clustered.push_back(i);
	const Step steps[] = {{"scattered", scattered}, {"clustered", clustered}};
	std::size_t chosen[3] = {};
	for (std::size_t s = 0; s < std::size(steps); ++s) {
		up.write("app", steps[s].keys, int(1000 * (s + 1)));
		cold();
		const auto &base = *builds[builds.size() - 2], &head = *builds.back();
		const auto stats = relay.pages();
		const auto frames = decode(answer(relay, {{"app", base.at(), true}}));
		check(frames.size() == 1);
		const auto diff = r::merkle_diff(head.tree(), head.pages(), base.tree(), base.pages(), head.limits());
		const auto transfer = r::delta_transfer(diff, head);
		const auto records = answer(relay, {{"app", base.at(), false}});
		const auto choice = detent::delivery::choose_encoding(transfer, records);
		++chosen[std::size_t(choice.encoding)];
		if (choice.encoding == detent::delivery::Encoding::Pages) {
			check(frames[0].kind == d::FrameKind::Pages && frames[0].after == base.at().position &&
				  frames[0].to == head.at() && frames[0].root == head.root());
			check(std::string_view(transfer.data(), transfer.size()) == frames[0].transfer);
			const auto got = receive(frames[0], head, held[held.size() - 2].get());
			check(got->transfer.tree.digest == head.tree().digest);
			check(relay.pages().deltas == stats.deltas + 1);
		} else {
			check(decode(records).front().kind == frames[0].kind && frames[0].kind != d::FrameKind::Pages);
			check(relay.pages().delta_records == stats.delta_records + 1);
		}
		std::fprintf(stderr, "delta %s: %zu keys, pages %zu raw %llu sent, records %zu raw %llu sent: %s\n",
					 steps[s].name, steps[s].keys.size(), transfer.size(),
					 static_cast<unsigned long long>(choice.pages_sent), records.size(),
					 static_cast<unsigned long long>(choice.records_sent),
					 choice.encoding == detent::delivery::Encoding::Pages ? "pages" : "records");
	}
	check(chosen[std::size_t(detent::delivery::Encoding::Records)] >= 1);
	// A base the window does not hold keeps records.
	auto between = builds.front()->at();
	++between.position;
	const auto outside = decode(answer(relay, {{"app", between, true}}));
	check(outside.size() == 1 && outside[0].kind != d::FrameKind::Pages && outside[0].after == between.position);
}

// The mirror reads records only: a pages frame from its upstream fails the sync, unretried.
void mirror_refuses(const std::filesystem::path &scratch) {
	prepare(scratch / "refuses");
	Upstream up;
	up.create("app", 10);
	const std::vector<std::string> names{"app"};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "refuses", 0644), cluster, up);
	relay.sync(names);
	const auto head = *relay.head("app");
	up.send_pages = true;
	const auto calls = up.frame_calls;
	bool refused = false;
	try {
		relay.sync(names);
	} catch (const r::UpstreamMoved &) {
		check(false);
	} catch (const std::runtime_error &error) {
		refused = std::string_view(error.what()).find("pages") != std::string_view::npos;
	}
	check(refused && up.frame_calls == calls + 1 && *relay.head("app") == head);
	std::fprintf(stderr, "mirror: a pages frame from the upstream is refused\n");
}
} // namespace

int main(int argc, char **argv) {
	check(argc == 2);
	const std::filesystem::path scratch = argv[1];
	try {
		std::filesystem::remove_all(scratch);
		std::filesystem::create_directories(scratch);
		full_copy(scratch);
		deltas(scratch);
		mirror_refuses(scratch);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_relay_pages: %s\n", error.what());
		return 1;
	}
	std::filesystem::remove_all(scratch);
	std::fprintf(stderr, "test_relay_pages: ok\n");
	return 0;
}
