// The relay's page answers (dense distribution DD3, D6, D7) over a scripted upstream that serves real
// history files. A reader that takes pages gets the full copy as a pages frame, and DD1's receiver
// rebuilds from it the relay's build: every pack byte for byte under the agent's derived ids, and its
// root. The relay builds once per head, over its previous head (D7.2): that build's root is a full
// build's, and its relocation (D7.4) a full build's files. The canonical copy stays at s while the
// delta from s is smaller than a fresh copy. A base its page window holds gets D6's smaller encoding.
// A position that keeps records gets the same bytes as from a relay that never built. A pages answer
// owns what it sends while a build lands. The mirror refuses a pages frame from its upstream. In the
// service, a build runs off the mirror thread: a records answer goes while a cold pages reader waits
// for the build.
#include "delta_transfer.hh"
#include "detent_local/derived_ids.hh"
#include "detent_relay/mirror_service.hh"
#include "detent_relay/namespace_relay.hh"
#include "history_format.hh"
#include "page_copy.hh"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <source_location>
#include <sys/stat.h>
#include <thread>

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
	// One seal rewriting keys [0, keep) to `salt + i` and deleting [keep, total).
	void rewrite_and_drop(const std::string &name, std::size_t total, std::size_t keep, int salt) {
		auto &n = namespaces_.at(name);
		const auto first = n.files.back().last + 1;
		h::Block block{name, identity('i'), first, first + total - 1, {}};
		for (std::size_t j = 0; j < total; ++j) {
			auto each = change(name, j, first + j, salt + int(j));
			if (j >= keep)
				each.definition.reset();
			block.changes.push_back(std::move(each));
		}
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
	void root(const d::RootDigest &root) override { frames.back().root = root; }
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
std::unique_ptr<Received> receive(const Frame &frame, const r::Generation &target, const Received *base = nullptr) {
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
// The full build of the state a records reader's answer carries, as the relay builds it cold.
std::shared_ptr<const r::Generation> reference_build(r::NamespaceRelay &relay, const std::string &name) {
	return r::build_generation(answer(relay, {{name, std::nullopt, false}}));
}
// The pages frame of `built`'s full copy, as a relay that holds it at s sends it.
Frame full_frame(const r::Generation &built) {
	const auto copy = r::full_copy(built);
	return {"", d::FrameKind::Pages, 0, 0, copy->at, copy->root, std::string(copy->transfer.begin(), copy->transfer.end())};
}
std::map<l::SegmentId, std::string_view> packs_of(const r::Generation &built) {
	std::map<l::SegmentId, std::string_view> out;
	for (const auto &segment : built.segments())
		out.emplace(segment.ownership.segment, segment.bytes->view());
	return out;
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
	check(full_frame(*built).transfer == frames[0].transfer);
	const auto got = receive(frames[0], *built);
	const auto packs = packs_of(*built);
	check(got->transfer.tree == built->tree() && got->transfer.segments.size() == packs.size());
	for (const auto &segment : got->transfer.segments)
		check(packs.at(segment.ownership.segment) == segment.bytes->view());

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
	up.create("app", 45000); // two segments: a clustered rewrite leaves one
	const std::vector<std::string> names{"app"};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "deltas", 0644), cluster, up);
	std::vector<std::shared_ptr<const r::Generation>> builds;
	std::vector<std::unique_ptr<Received>> held;
	// A cold reader gets the canonical copy, at s, and starts the build at the head. An agent that
	// holds each head is its full build's full copy.
	auto cold = [&] {
		relay.sync(names);
		const auto frames = decode(answer(relay, {{"app", std::nullopt, true}}));
		check(frames.size() == 1 && frames[0].kind == d::FrameKind::Pages);
		builds.push_back(reference_build(relay, "app"));
		held.push_back(receive(full_frame(*builds.back()), *builds.back()));
		check(held.back()->transfer.tree.digest == builds.back()->tree().digest);
		return frames[0];
	};
	const auto first = cold();
	check(first.to == builds[0]->at() && first.transfer == full_frame(*builds[0]).transfer);
	struct Step {
		const char *name;
		std::vector<std::size_t> keys;
	};
	std::vector<std::size_t> scattered, clustered;
	for (std::size_t i = 0; i < 45000; i += 9000)
		scattered.push_back(i);
	for (std::size_t i = 0; i < 36000; ++i)
		clustered.push_back(i);
	const Step steps[] = {{"scattered", scattered}, {"clustered", clustered}};
	std::size_t chosen[3] = {};
	for (std::size_t s = 0; s < std::size(steps); ++s) {
		up.write("app", steps[s].keys, int(1000 * (s + 1)));
		const auto relocations = relay.pages().relocations;
		const auto copy = cold();
		const auto &base = *builds[builds.size() - 2], &head = *builds.back();
		// The head is built over the previous one, and the copy stays at s: the delta from s is smaller
		// than a fresh copy.
		check(relay.pages().incremental == s + 1 && relay.pages().relocations == relocations);
		check(copy.to == builds[0]->at() && copy.transfer == first.transfer);
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
	using detent::delivery::Encoding;
	check(chosen[std::size_t(Encoding::Records)] && chosen[std::size_t(Encoding::Pages)]);
	// A rewrite of every key makes the delta from s no smaller than a fresh copy: the new head is
	// relocated into the new copy, whose files are a full build's.
	std::vector<std::size_t> every(45000);
	for (std::size_t i = 0; i < every.size(); ++i)
		every[i] = i;
	up.write("app", every, 9000);
	const auto relocations = relay.pages().relocations;
	cold();
	check(relay.pages().relocations == relocations + 1);
	const auto rewritten = decode(answer(relay, {{"app", std::nullopt, true}}));
	check(rewritten.size() == 1 && rewritten[0].to == builds.back()->at() &&
		  rewritten[0].transfer == full_frame(*builds.back()).transfer);
	// A base the window does not hold keeps records.
	auto between = builds.front()->at();
	++between.position;
	const auto outside = decode(answer(relay, {{"app", between, true}}));
	check(outside.size() == 1 && outside[0].kind != d::FrameKind::Pages && outside[0].after == between.position);
}

// D7.2 and D7.4: a build over the previous generation from the records after it has a full build's
// root and shares the base's segments it still reaches; its relocation writes a full build's files.
void incremental(const std::filesystem::path &scratch) {
	prepare(scratch / "incremental");
	Upstream up;
	up.create("app", 3000);
	const std::vector<std::string> names{"app"};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "incremental", 0644), cluster, up);
	relay.sync(names);
	auto base = reference_build(relay, "app");
	std::vector<std::size_t> scattered, clustered;
	for (std::size_t i = 0; i < 3000; i += 300)
		scattered.push_back(i);
	for (std::size_t i = 1000; i < 1600; ++i)
		clustered.push_back(i);
	const auto shared = [](const r::Generation &a, const r::Generation &b) {
		std::size_t count = 0;
		const auto held = packs_of(a);
		for (const auto &segment : b.segments())
			count += held.contains(segment.ownership.segment);
		return count;
	};
	for (int step = 0; step < 4; ++step) {
		if (step == 0)
			up.write("app", scattered, 500);
		else if (step == 1)
			up.write("app", clustered, 600);
		else if (step == 2)
			up.rewrite_and_drop("app", 3000, 1200, 700);
		else
			up.write("app", {1, 2, 2999}, 800); // brings a deleted key back
		relay.sync(names);
		const auto body = answer(relay, {{"app", base->at(), false}});
		const auto built = r::build_generation(body, base);
		const auto full = reference_build(relay, "app");
		check(built->at() == full->at() && built->root() == full->root() && built->tree().digest == full->tree().digest &&
			  built->page_bytes() == full->page_bytes());
		if (step < 2)
			check(shared(*base, *built) > 0);
		const auto moved = r::relocate(*built);
		check(moved->root() == full->root() && packs_of(*moved) == packs_of(*full) &&
			  full_frame(*moved).transfer == full_frame(*full).transfer);
		std::fprintf(stderr,
					 "incremental %d: root equal, %zu of %zu segments shared with the base, %llu physical bytes for %llu "
					 "page bytes; relocation equals the full build's files\n",
					 step, shared(*base, *built), built->segments().size(),
					 static_cast<unsigned long long>(built->physical_bytes()),
					 static_cast<unsigned long long>(built->page_bytes()));
		base = built;
	}
}

// A pages answer owns what it sends: in one answer for two namespaces, the second's build lands the
// first's newer head and a new canonical copy, which replaces the copy the first's frame names; the
// frame still carries that copy's bytes. The runner holds each build until the next is submitted,
// which forces that order.
void answer_owns(const std::filesystem::path &scratch) {
	prepare(scratch / "owns");
	Upstream up;
	up.create("app", 3000);
	up.create("beta", 40);
	std::vector<std::size_t> all(3000);
	for (std::size_t i = 0; i < all.size(); ++i)
		all[i] = i;
	up.write("app", all, 100000);
	const std::vector<std::string> names{"app", "beta"};
	std::vector<std::function<void()>> queued;
	auto run_queued = [&] {
		auto now = std::move(queued);
		queued.clear();
		for (auto &task : now)
			task();
	};
	r::NamespaceRelay relay(std::make_shared<c::PosixIO>(scratch / "owns", 0644), cluster, up, {},
							[&](std::function<void()> task) {
								run_queued();
								queued.push_back(std::move(task));
							});
	relay.sync(names);
	check(decode(answer(relay, {{"app", std::nullopt, true}})).empty());
	decode(answer(relay, {{"beta", std::nullopt, true}}));
	run_queued();
	relay.collect();
	const auto first = decode(answer(relay, {{"app", std::nullopt, true}}));
	check(first.size() == 1 && first[0].kind == d::FrameKind::Pages);
	const auto h1 = reference_build(relay, "app");
	check(first[0].to == h1->at() && first[0].transfer == full_frame(*h1).transfer);
	// h2 rewrites every app key and drops half, so its build relocates into a new copy at h2.
	up.rewrite_and_drop("app", 3000, 1500, 7);
	up.write("beta", {0, 1, 2}, 9);
	relay.sync(names);
	const auto relocations = relay.pages().relocations;
	const auto both = decode(answer(relay, {{"app", std::nullopt, true}, {"beta", std::nullopt, true}}));
	check(relay.pages().relocations == relocations + 1);
	check(both.size() == 2 && both[0].name == "app" && both[0].kind == d::FrameKind::Pages);
	check(both[0].to == h1->at() && both[0].root == h1->root() && both[0].transfer == first[0].transfer);
	check(both[1].name == "beta" && both[1].kind == d::FrameKind::Pages);
	run_queued();
	relay.collect();
	const auto h2 = reference_build(relay, "app");
	const auto now = decode(answer(relay, {{"app", std::nullopt, true}}));
	check(now.size() == 1 && now[0].to == h2->at() && now[0].transfer == full_frame(*h2).transfer);
	std::fprintf(stderr, "answer owns: the copy a frame names outlives its replacement within the answer\n");
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
// The upstream a MirrorService owns: the test's, which the mirror thread alone reads while it runs.
class Forward final : public r::Upstream {
  public:
	explicit Forward(Upstream &to) : to_(to) {}
	r::MirrorTable layouts(std::span<const std::string> names) override { return to_.layouts(names); }
	std::string fetch(std::string_view name_space, const r::MirrorFile &file) override {
		return to_.fetch(name_space, file);
	}
	std::string frames(std::span<const r::Position> positions) override { return to_.frames(positions); }

  private:
	Upstream &to_;
};
using Clock = std::chrono::steady_clock;
template <class Done> void until(Done done, const char *what) {
	for (const auto limit = Clock::now() + std::chrono::seconds(60); !done();
		 std::this_thread::sleep_for(std::chrono::milliseconds(1)))
		if (Clock::now() > limit)
			throw std::runtime_error(std::string("timed out waiting for ") + what);
}
d::Identity session_of(const r::MirrorJob &job) {
	const auto at = job.reply.body.find("\"session\":\"");
	check(job.reply.status == 200 && at != std::string::npos);
	d::Identity out{};
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = char(std::stoi(job.reply.body.substr(at + 11 + 2 * i, 2), nullptr, 16));
	return out;
}

// The service builds on its own thread: while a cold pages reader of a large namespace waits for the
// build, a records reader of another is answered. A session that lists the namespace in "records"
// gets records (D9's fallback), and no build or pages answer.
void service_builds_aside(const std::filesystem::path &scratch, std::size_t keys) {
	prepare(scratch / "service");
	Upstream up;
	up.create("big", keys);
	up.create("small", 40);
	r::MirrorServiceOptions options;
	options.directory = scratch / "service";
	options.cluster = cluster;
	options.namespaces = {"big", "small"};
	options.interval = std::chrono::milliseconds(10);
	r::MirrorService service(options, std::make_unique<Forward>(up), std::make_shared<d::StorageLedger>(std::size_t{1} << 32));
	until([&] { return service.status().ready; }, "the first sync");
	const std::string_view patterns[]{"big", "small"};
	const d::NamespaceGrants grants(patterns);
	auto submit = [&](r::MirrorJob::Kind kind, d::Identity session, std::vector<r::Position> positions, bool pages,
					  std::vector<std::string> records) {
		auto job = std::make_shared<r::MirrorJob>();
		job->kind = kind;
		job->grants = &grants;
		job->session = session;
		job->positions = std::move(positions);
		job->pages = pages;
		job->records = std::move(records);
		job->wait_ms = 30000;
		service.submit(job);
		return job;
	};
	auto open = [&](bool pages, std::vector<std::string> records = {}) {
		auto job = submit(r::MirrorJob::Kind::Open, {}, {}, pages, std::move(records));
		until([&] { return job->done.load(); }, "an open");
		return session_of(*job);
	};
	const auto paged = open(true), plain = open(false);
	const auto started = Clock::now();
	auto cold = submit(r::MirrorJob::Kind::Next, paged, {{"big", std::nullopt, false}}, false, {});
	auto quick = submit(r::MirrorJob::Kind::Next, plain, {{"small", std::nullopt, false}}, false, {});
	until([&] { return quick->done.load(); }, "the records answer");
	const auto answered = Clock::now();
	const bool building = !cold->done.load();
	until([&] { return cold->done.load(); }, "the pages answer");
	const auto built = Clock::now();
	check(building && quick->reply.status == 200 && cold->reply.status == 200);
	until([&] { return service.status().pages.full_copies == 1; }, "the pages count");
	check(service.status().pages.builds == 1);

	const auto fallback = open(true, {"big"});
	auto records = submit(r::MirrorJob::Kind::Next, fallback, {{"big", std::nullopt, false}}, false, {});
	until([&] { return records->done.load(); }, "the fallback");
	check(records->reply.status == 200);
	const auto state = service.status();
	check(state.pages.builds == 1 && state.pages.full_copies == 1 && state.pages.build_failures == 0);
	using std::chrono::duration_cast;
	using std::chrono::milliseconds;
	std::fprintf(stderr, "service: %zu keys; records answer in %lld ms while the build ran, pages answer in %lld ms\n",
				 keys, static_cast<long long>(duration_cast<milliseconds>(answered - started).count()),
				 static_cast<long long>(duration_cast<milliseconds>(built - started).count()));
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
		incremental(scratch);
		answer_owns(scratch);
		mirror_refuses(scratch);
		service_builds_aside(scratch, 100000);
	} catch (const std::exception &error) {
		std::fprintf(stderr, "test_relay_pages: %s\n", error.what());
		return 1;
	}
	std::filesystem::remove_all(scratch);
	std::fprintf(stderr, "test_relay_pages: ok\n");
	return 0;
}
