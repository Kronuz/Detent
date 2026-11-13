#include "history.hh"
#include <algorithm>
#include <stdexcept>

namespace detent::authority::history {
namespace {
void require(bool condition, const char *message) {
	if (!condition)
		throw std::runtime_error(message);
}
constexpr std::string_view temporary = ".tmp-";
std::string read_all(kronuz::journal::File &file) {
	std::string bytes(static_cast<std::size_t>(file.size()), '\0');
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		auto count = file.read_at(offset, std::span<char>(bytes.data() + offset, bytes.size() - offset));
		require(count && count <= bytes.size() - offset, "short history read");
		offset += count;
	}
	return bytes;
}
bool history_file(std::string_view name) {
	return name.starts_with("seal-") || name.starts_with("block-") || name.starts_with("base-");
}
std::uint64_t seal_index(std::string_view name) {
	std::uint64_t index = 0;
	for (char c : name.substr(5))
		index = index * 16 + static_cast<std::uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
	return index;
}
// Every file a table reaches, by name, with the digest it must have (a seal file verifies itself).
std::vector<std::pair<std::string, std::optional<Digest>>> reached(const Table &t) {
	std::vector<std::pair<std::string, std::optional<Digest>>> out;
	for (const auto &n : t.namespaces) {
		if (n.base.bytes)
			out.emplace_back(base_name(n.name, n.incarnation, n.base.last), n.base.digest);
		for (const auto &b : n.blocks)
			out.emplace_back(block_name(n.name, n.incarnation, b.first, b.last), b.digest);
		for (const auto &b : n.level0)
			out.emplace_back(seal_name(b.seal), std::nullopt);
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}
} // namespace

Directory::Directory(std::shared_ptr<kronuz::journal::IO> io, Limits limits) : io_(std::move(io)), limits_(limits) {
	if (!io_)
		throw std::invalid_argument("missing history directory");
}
std::optional<std::string> Directory::find(std::string_view name) {
	auto file = io_->open_reclaim_candidate_read_only(name);
	if (!file)
		return std::nullopt;
	return read_all(*file);
}
std::string Directory::read(std::string_view name) {
	auto bytes = find(name);
	if (!bytes)
		throw std::runtime_error("absent history file " + std::string(name));
	return std::move(*bytes);
}
void Directory::publish(const std::vector<std::pair<std::string, std::string>> &files) {
	std::vector<std::unique_ptr<kronuz::journal::File>> handles;
	std::vector<std::string_view> names;
	for (const auto &[name, bytes] : files) {
		if (auto existing = find(name); existing && *existing == bytes)
			continue; // A reused name holding these bytes was synced before it was linked.
		auto tmp = std::string(temporary) + name;
		if (io_->open_reclaim_candidate_read_only(tmp))
			io_->remove(tmp);
		auto file = io_->create_exclusive(tmp);
		std::string_view rest = bytes;
		std::uint64_t offset = 0;
		while (!rest.empty()) {
			auto count = file->write_at(offset, rest);
			require(count && count <= rest.size(), "short history write");
			offset += count;
			rest.remove_prefix(count);
		}
		handles.push_back(std::move(file));
		names.push_back(name);
	}
	if (handles.empty())
		return;
	std::vector<kronuz::journal::File *> group;
	for (auto &h : handles)
		group.push_back(h.get());
	io_->sync_files(group);
	// A name that exists with other bytes is an orphan the durable table does not reach.
	for (auto name : names)
		io_->replace(std::string(temporary) + std::string(name), name);
	io_->sync_directory();
}
void Directory::remove(std::string_view name) { io_->remove(name); }
std::vector<std::string> Directory::names() {
	std::vector<std::string> out;
	auto cursor = io_->scan_directory();
	while (auto name = cursor->next())
		out.push_back(std::move(*name));
	std::sort(out.begin(), out.end());
	return out;
}
void Directory::remove_temporaries() {
	for (const auto &name : names())
		if (name.starts_with(temporary))
			io_->remove(name);
}

History::History(std::shared_ptr<kronuz::journal::IO> io, Limits limits, Fetch fetch)
	: directory_(std::move(io), limits), limits_(limits), fetch_(std::move(fetch)), layout_(limits) {}

std::string History::verified(std::string_view name, const std::optional<Digest> &digest) {
	auto good = [&](const std::string &bytes) {
		if (digest)
			return sha256(bytes) == *digest;
		try {
			decode_seal(bytes, limits_);
			return true;
		} catch (const std::invalid_argument &) {
			return false;
		}
	};
	if (auto local = directory_.find(name); local && good(*local))
		return std::move(*local);
	// A peer holds the same bytes under the same name.
	if (fetch_)
		if (auto fetched = fetch_(name); fetched && good(*fetched)) {
			directory_.publish({{std::string(name), *fetched}});
			return std::move(*fetched);
		}
	throw std::runtime_error("history file " + std::string(name) + " is missing or rotten and no peer holds it");
}

void History::recover(const State &state) {
	ready_ = false;
	layout_ = Layout(limits_);
	durable_ = {};
	directory_.remove_temporaries();
	auto table = directory_.find(table_name);
	if (!state.positions_enabled()) {
		// Replay from before the enabling entry folds none of the seals a table holds.
		if (table)
			adopt(decode_table(*table, limits_), state);
		ready_ = true;
		return;
	}
	require(state.seal().index == state.applied_index(), "a checkpoint between seals");
	if (!table && fetch_)
		table = fetch_(table_name);
	if (!table) {
		// A checkpoint at the enabling entry may precede its table: take the first bases again.
		require(!state.seal().ordinal, "the history table is missing and no peer holds it");
		ready_ = true;
		seal(state);
		return;
	}
	adopt(decode_table(*table, limits_), state);
}

// Adopts a table: every file it reaches verified (or fetched), then the seal files
// through the state's seal. A table past the state's seal already holds the seals
// that replay re-applies; those fold nothing (D6 "Recovery").
void History::adopt(Table table, const State &state) {
	for (const auto &[name, digest] : reached(table))
		verified(name, digest);
	durable_ = std::move(table);
	layout_ = Layout(durable_, limits_);
	if (durable_.index < state.seal().index)
		chain(state);
	else
		require(durable_.index > state.seal().index || durable_.ordinal == state.seal().ordinal,
				"the history table and the checkpoint disagree");
	// Every file the table reaches is durable before the table that names it.
	directory_.publish({{std::string(table_name), encode(durable_)}});
	ready_ = true;
	collect();
}

// Replays the seal files above the table through the state's seal, newest named first.
void History::chain(const State &state) {
	std::vector<std::string> seals;
	for (auto index = state.seal().index; index > layout_.table().index;) {
		auto bytes = verified(seal_name(index), std::nullopt);
		index = decode_seal(bytes, limits_).previous;
		seals.push_back(std::move(bytes));
	}
	for (auto it = seals.rbegin(); it != seals.rend(); ++it) {
		auto step = layout_.replay(*it, directory_);
		directory_.publish(step.written); // Rebuilds an output a crash left missing.
		if (step.table) {
			directory_.publish({{std::string(table_name), encode(*step.table)}});
			durable_ = *step.table;
		}
	}
	require(layout_.table().index == state.seal().index && layout_.table().ordinal == state.seal().ordinal,
			"the seal chain does not reach the checkpoint");
}

void History::install(const State &state) {
	ready_ = false;
	layout_ = Layout(limits_);
	durable_ = {};
	if (!state.positions_enabled()) {
		ready_ = true;
		return;
	}
	require(state.seal().index == state.applied_index(), "a snapshot between seals");
	require(bool(fetch_), "an install needs a peer's history");
	auto table = fetch_(table_name);
	require(table.has_value(), "no peer holds a history table");
	directory_.remove_temporaries();
	adopt(decode_table(*table, limits_), state);
}

void History::seal(const State &sealed) {
	require(ready_, "history unavailable");
	if (sealed.applied_index() <= layout_.table().index)
		return; // The adopted table already holds this seal.
	try {
		auto step = layout_.seal(sealed, directory_);
		directory_.publish({{seal_name(step.seal.index), step.bytes}}); // In seal order: all a checkpoint waits for.
		directory_.publish(step.written);
		if (step.table) {
			directory_.publish({{std::string(table_name), encode(*step.table)}});
			durable_ = std::move(*step.table);
			collect();
		}
	} catch (...) {
		ready_ = false;
		throw;
	}
}

// Unlinks what the durable table does not reach, keeping every seal file above it and its outputs.
void History::collect() {
	std::set<std::string, std::less<>> keep;
	for (const auto &[name, _] : reached(durable_))
		keep.insert(name);
	auto names = directory_.names();
	for (const auto &name : names)
		if (name.starts_with("seal-") && seal_index(name) > durable_.index) {
			keep.insert(name);
			try {
				for (const auto &o : decode_seal(directory_.read(name), limits_).outputs)
					keep.insert(o.name());
			} catch (const std::invalid_argument &) {
				// A rotten seal file above the table: its outputs stay until it is rebuilt.
				return;
			}
		}
	for (const auto &name : names)
		if (history_file(name) && !keep.contains(name))
			directory_.remove(name);
}
} // namespace detent::authority::history
