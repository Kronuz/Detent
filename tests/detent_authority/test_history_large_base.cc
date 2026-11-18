// Every history file fits a peer fetch (durability review F2). The lane carries
// `history::fetch_bytes` (the image bound): a configuration's peers take it and a service
// refuses less. A policy whose base, block, seal file or table bound exceeds it is refused,
// each kind alone. Under the default Limits one namespace may hold every live byte, so its base
// is above consensus's default 64 MiB `fetch_bytes`: a voter installs a 73 MB base, an 8 MB
// seal file and the block it rolls up through the configured lane, served by `history_reader`.
// A file the lane cannot carry ends the install with a reported error.
#include "service.hh"
#include "peer_history.hh"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <journal/memory_io.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

using namespace detent::authority;
namespace h = detent::authority::history;
namespace mem = kronuz::journal::testing;

namespace {
// A three-voter configuration for node 1 with the default limits.
ServiceOptions load() {
	const auto config =
		std::filesystem::current_path() / ".scratch" / ("history-large-base-" + std::to_string(::getpid()) + ".json");
	std::filesystem::create_directories(config.parent_path());
	auto voter = [](unsigned id) {
		return "{\"id\":" + std::to_string(id) + ",\"host\":\"127.0.0." + std::to_string(id) +
			   "\",\"port\":7001,\"certificate_sha256\":\"" + std::string(64, 'a') + "\"}";
	};
	{
		std::ofstream file(config);
		file << "{\"cluster\":\"63000000000000000000000000000000\",\"group\":\"67000000000000000000000000000000\","
				"\"epoch\":\"65000000000000000000000000000000\",\"directory\":\"state\",\"node\":1,\"voters\":["
			 << voter(1) << "," << voter(2) << "," << voter(3)
			 << "],\"peer_tls\":{\"certificate\":\"c\",\"key\":\"k\",\"ca\":\"a\"},"
				"\"publisher_tls\":{\"certificate\":\"c\",\"key\":\"k\",\"ca\":\"a\"},\"credentials\":[{"
				"\"token\":\"0123456789abcdef0123456789abcdef\",\"producer\":\"agent\",\"namespaces\":[\"ns\"]}]}";
	}
	assert(::chmod(config.c_str(), 0600) == 0);
	auto options = load_service_options(config);
	std::filesystem::remove(config);
	return options;
}
// One peer behind a lane of `bound` bytes each way, as consensus's fetch lane answers: a
// reply its reader refuses fails, as does one above the requester's bound.
h::Fetch lane(std::shared_ptr<kronuz::journal::IO> source, std::size_t bound, std::size_t &served) {
	auto read = history_reader(std::move(source), bound);
	return [read, bound, &served](const std::string &name, h::Check good, h::Fetched done) {
		std::optional<std::string> bytes;
		try {
			bytes = read(name);
		} catch (const std::exception &) {
			return done({std::nullopt, "voter 1 io_error"});
		}
		if (bytes && bytes->size() > bound)
			return done({std::nullopt, "voter 1 message_size"});
		if (!bytes || !good(*bytes))
			return done({std::nullopt, bytes ? "voter 1 rotten" : "voter 1 none"});
		served = std::max(served, bytes->size());
		done({std::move(bytes), {}});
	};
}
void drain(h::History &history) {
	while (auto operation = history.take_io_operation()) {
		kronuz::journal::drive_synchronously(*operation);
		history.settle();
	}
}
} // namespace

int main() {
	const auto loaded = load();
	const Limits limits = loaded.node.application;
	assert(limits.state_bytes == Limits{}.state_bytes && loaded.peers);
	const auto fetch_bytes = loaded.peers->fetch_bytes;
	{
		// A service refuses peers whose lane cannot carry a base of `state_bytes`.
		auto low = loaded;
		low.peers->fetch_bytes = limits.state_bytes;
		bool refused = false;
		try {
			Service service(std::move(low));
		} catch (const std::invalid_argument &error) {
			refused = std::string(error.what()).find("fetch_bytes") != std::string::npos;
		}
		assert(refused);
	}
	{
		// Under the default policy the base is the largest file, and it fits the lane.
		const auto bounds = h::file_bounds(limits, LayoutPolicy{});
		assert(h::file_bytes(limits, LayoutPolicy{}) == bounds.base && bounds.base <= h::fetch_bytes(limits));
		assert(bounds.block < bounds.base && bounds.seal < bounds.base && bounds.table < bounds.base);
		// One kind at a time outgrows the lane: α (a block), carry_bytes (a seal file), P (the table).
		auto big_block = LayoutPolicy{}, big_seal = LayoutPolicy{}, big_table = LayoutPolicy{};
		big_block.alpha_num = 65536;
		big_block.alpha_den = 1;
		big_seal.carry_bytes = std::uint64_t{1} << 32;
		big_table.period = 1u << 20;
		const auto lane = h::fetch_bytes(limits);
		const std::array<std::pair<LayoutPolicy, int>, 3> oversized{
			{{big_block, 1}, {big_seal, 2}, {big_table, 3}}};
		for (const auto &[policy, kind] : oversized) {
			const auto b = h::file_bounds(limits, policy);
			assert((b.block > lane) == (kind == 1) && (b.seal > lane) == (kind == 2) && (b.table > lane) == (kind == 3));
			// The enabling or layout entry carrying it is refused, and a node configured with it.
			Context context;
			context.cluster[0] = 'c';
			context.group[0] = 'g';
			context.epoch[0] = 'e';
			AdministrativeCommand enable{context, AdministrativeKind::EnablePositions, {}, {}, {}, policy};
			enable.nonce[0] = 's';
			bool refused = false;
			try {
				encode(enable, limits);
			} catch (const std::invalid_argument &error) {
				refused = std::string(error.what()).find("peer fetch") != std::string::npos;
			}
			assert(refused);
			NodeOptions options = loaded.node;
			options.application.layout = policy;
			refused = false;
			try {
				validate_node_options(options);
			} catch (const std::invalid_argument &error) {
				refused = std::string(error.what()).find("peer fetch") != std::string::npos;
			}
			assert(refused);
			// Across a change the wider parameters bound what is written.
			assert(h::file_bytes(limits, LayoutPolicy{}, policy) >= h::file_bytes(limits, policy));
		}
	}

	// One namespace of 330,000 keys at 197 bytes each: 73,260,000 live bytes, 55% of state_bytes.
	Context context;
	context.cluster[0] = 'c';
	context.group[0] = 'g';
	context.epoch[0] = 'e';
	State s(context);
	std::uint64_t index = 0, sequence = 0;
	Command open;
	open.context = context;
	open.kind = Kind::Open;
	open.producer = "p";
	open.nonce[0] = 'n';
	s.apply(++index, open);
	s.close_entry(0);
	const std::string pad(186, 'x');
	for (std::size_t k = 0; k < 330000;) {
		Command c;
		c.context = context;
		c.producer = "p";
		c.generation = 1;
		c.sequence = ++sequence;
		c.name_space = "big";
		for (unsigned m = 0; m < 128 && k < 330000; ++m, ++k)
			c.mutations.push_back({"big:" + pad + std::to_string(1000000 + k), detent::KnobDef{1}, {}, {}});
		assert(s.apply(++index, c).status == Status::Applied);
		s.close_entry(64);
	}
	assert(s.logical_bytes() == 73260000);
	const State start = s;
	AdministrativeCommand enable{context, AdministrativeKind::EnablePositions, {}, {}, {}, LayoutPolicy{}};
	enable.nonce[0] = 's';
	assert(s.administer(++index, enable).status == Status::Applied);
	bool sealed = s.close_entry(64);
	for (unsigned n = 0; !sealed && n < 100000; ++n) {
		s.advance(++index);
		sealed = s.close_entry(0);
	}
	assert(sealed && s.positions_enabled() && s.seal().index == s.applied_index());

	// The source voter writes the base at seal 0, then a seal of about seal_bytes of rewrites,
	// whose level-0 passes L, so the namespace rolls up into a block.
	mem::Disk source_disk;
	auto source = std::make_shared<mem::MemoryIO>(source_disk, "history/");
	h::History writer(source, limits);
	writer.recover(start);
	drain(writer);
	writer.seal(s);
	drain(writer);
	sealed = false;
	for (std::size_t k = 0; !sealed;) {
		Command c;
		c.context = context;
		c.producer = "p";
		c.generation = 1;
		c.sequence = ++sequence;
		c.name_space = "big";
		for (unsigned m = 0; m < 128; ++m, ++k)
			c.mutations.push_back({"big:" + pad + std::to_string(1000000 + k), detent::KnobDef{2}, {}, {}});
		assert(s.apply(++index, c).status == Status::Applied);
		sealed = s.close_entry(64);
	}
	writer.seal(s);
	drain(writer);
	assert(writer.durable_through(s.applied_index()));
	h::Directory directory(source, limits);
	const auto bounds = h::file_bounds(limits, LayoutPolicy{});
	std::size_t largest = 0, seal_file = 0, block = 0;
	for (const auto &name : directory.names()) {
		const auto size = directory.read(name).size();
		largest = std::max(largest, size);
		if (name.starts_with("base-"))
			assert(size == 73260051 && size > std::size_t{64} * 1024 * 1024 && size <= bounds.base);
		else if (name.starts_with("seal-")) {
			assert(size <= bounds.seal);
			seal_file = std::max(seal_file, size);
		} else if (name.starts_with("block-")) {
			assert(size <= bounds.block);
			block = std::max(block, size);
		} else
			assert(name == "layout" && size <= bounds.table);
	}
	assert(largest == 73260051 && seal_file > 8000000 && block > 8000000);

	// A voter with an empty history directory installs it through the configured lane.
	mem::Disk disk;
	auto target = std::make_shared<mem::MemoryIO>(disk, "history/");
	std::size_t served = 0;
	{
		h::History history(target, limits, lane(source, fetch_bytes, served));
		try {
			history.install(s);
		} catch (const std::exception &error) {
			std::cerr << "install failed: " << error.what() << "\n";
			return 1;
		}
		drain(history);
		if (!history.ready() || !history.durable_through(s.applied_index())) {
			std::cerr << "install failed: "
					  << (history.failure() ? "a write stalled" : "history is not durable through the snapshot")
					  << "\n";
			return 1;
		}
	}
	assert(served == largest && fetch_bytes == h::fetch_bytes(limits) && fetch_bytes >= bounds.base);
	h::Directory installed(target, limits);
	assert(installed.names() == directory.names());
	for (const auto &name : directory.names())
		assert(installed.read(name) == directory.read(name));

	// Through consensus's default 64 MiB lane the base cannot be fetched: the install ends,
	// naming the file and the voter's answer.
	{
		mem::Disk short_disk;
		std::size_t unused = 0;
		h::History history(std::make_shared<mem::MemoryIO>(short_disk, "history/"), limits,
						   lane(source, std::size_t{64} * 1024 * 1024, unused));
		std::string why;
		try {
			history.install(s);
		} catch (const std::runtime_error &error) {
			why = error.what();
		}
		assert(why.find("base-") != std::string::npos && why.find("io_error") != std::string::npos &&
			   !history.ready() && !history.restoring());
	}
	std::cout << "a " << largest << "-byte base (55% of state_bytes " << limits.state_bytes << "), a " << seal_file
			  << "-byte seal file and a " << block << "-byte block installed through a lane of " << fetch_bytes
			  << " bytes; bounds under the default policy: base " << bounds.base << ", block " << bounds.block
			  << ", seal file " << bounds.seal << ", table " << bounds.table << "; the base refused at 64 MiB\n";
}
