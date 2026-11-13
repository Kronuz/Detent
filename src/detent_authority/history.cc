#include "history.hh"
#include <algorithm>
#include <coroutine>
#include <limits>
#include <set>
#include <stdexcept>

namespace detent::authority::history {
namespace {
using kronuz::journal::MutationCompletion;
using kronuz::journal::MutationRequest;
using kronuz::journal::PrimitiveKind;
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

// A write as a coroutine: each co_await issues one primitive and resumes with its completion.
struct Script {
	struct promise_type {
		MutationRequest request;
		MutationCompletion result;
		std::exception_ptr error;
		Script get_return_object() noexcept { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept { error = std::current_exception(); }
	};
	std::coroutine_handle<promise_type> handle;
};
struct Issue {
	MutationRequest request;
	std::coroutine_handle<Script::promise_type> handle{};
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<Script::promise_type> h) noexcept {
		handle = h;
		h.promise().request = std::move(request);
	}
	MutationCompletion await_resume() {
		auto result = std::move(handle.promise().result);
		if (result.error)
			std::rethrow_exception(result.error);
		return result;
	}
};
MutationRequest primitive(PrimitiveKind kind, std::string_view source = {}) {
	MutationRequest r;
	r.kind = kind;
	r.source = source;
	return r;
}
// The same commit as Directory::publish, then the collector against the write's table:
// it keeps what the table reaches and every seal file above it and their outputs.
Script run(const Write &write, std::set<std::string, std::less<>> keep) {
	std::vector<std::string> names;
	if (write.temporaries) {
		auto cursor = std::shared_ptr<kronuz::journal::DirectoryCursor>((co_await Issue{primitive(PrimitiveKind::Scan)}).cursor);
		for (;;) {
			auto next = primitive(PrimitiveKind::Next);
			next.cursor = cursor;
			auto name = (co_await Issue{std::move(next)}).name;
			if (!name)
				break;
			names.push_back(std::move(*name));
		}
		cursor.reset();
		for (const auto &name : names)
			if (name.starts_with(temporary))
				co_await Issue{primitive(PrimitiveKind::Remove, name)};
	}
	for (const auto &group : write.groups) {
		std::vector<std::shared_ptr<kronuz::journal::File>> handles;
		std::vector<std::string> written;
		for (const auto &[name, bytes] : group) {
			auto open = primitive(PrimitiveKind::OpenCandidate, name);
			open.read_only = true;
			if (auto existing = std::shared_ptr<kronuz::journal::File>((co_await Issue{std::move(open)}).file)) {
				auto size = primitive(PrimitiveKind::Size);
				size.file = existing;
				std::string held((co_await Issue{std::move(size)}).length, '\0');
				for (std::size_t offset = 0; offset < held.size();) {
					auto read = primitive(PrimitiveKind::Read);
					read.file = existing;
					read.offset = offset;
					read.destination_bytes = std::span<char>(held.data() + offset, held.size() - offset);
					auto count = (co_await Issue{std::move(read)}).count;
					require(count && count <= held.size() - offset, "short history read");
					offset += count;
				}
				if (held == bytes)
					continue; // A reused name holding these bytes was synced before it was linked.
			}
			auto tmp = std::string(temporary) + name;
			auto stale = primitive(PrimitiveKind::OpenCandidate, tmp);
			stale.read_only = true;
			if ((co_await Issue{std::move(stale)}).file)
				co_await Issue{primitive(PrimitiveKind::Remove, tmp)};
			auto file = std::shared_ptr<kronuz::journal::File>((co_await Issue{primitive(PrimitiveKind::Create, tmp)}).file);
			for (std::size_t offset = 0; offset < bytes.size();) {
				auto put = primitive(PrimitiveKind::Write);
				put.file = file;
				put.offset = offset;
				put.bytes = std::string_view(bytes).substr(offset);
				auto count = (co_await Issue{std::move(put)}).count;
				require(count && count <= bytes.size() - offset, "short history write");
				offset += count;
			}
			handles.push_back(std::move(file));
			written.push_back(std::move(tmp));
		}
		if (handles.empty())
			continue;
		auto sync = primitive(PrimitiveKind::SyncFiles);
		sync.files = handles;
		co_await Issue{std::move(sync)};
		handles.clear();
		// A name that exists with other bytes is an orphan the durable table does not reach.
		for (const auto &tmp : written) {
			auto rename = primitive(PrimitiveKind::Replace, tmp);
			rename.destination = std::string_view(tmp).substr(temporary.size());
			co_await Issue{std::move(rename)};
		}
		co_await Issue{primitive(PrimitiveKind::DirectorySync)};
	}
	if (!write.table)
		co_return;
	names.clear();
	auto cursor = std::shared_ptr<kronuz::journal::DirectoryCursor>((co_await Issue{primitive(PrimitiveKind::Scan)}).cursor);
	for (;;) {
		auto next = primitive(PrimitiveKind::Next);
		next.cursor = cursor;
		auto name = (co_await Issue{std::move(next)}).name;
		if (!name)
			break;
		names.push_back(std::move(*name));
	}
	cursor.reset();
	std::sort(names.begin(), names.end());
	for (const auto &name : names)
		if (history_name(name) && name != table_name && !keep.contains(name))
			co_await Issue{primitive(PrimitiveKind::Remove, name)};
}
class Operation final : public kronuz::journal::IOOperation {
  public:
	Operation(std::shared_ptr<kronuz::journal::IO> io, std::shared_ptr<const Write> write,
			  std::set<std::string, std::less<>> keep)
		: io_(std::move(io)), write_(std::move(write)), script_(run(*write_, std::move(keep))) {
		advance();
	}
	~Operation() override { script_.handle.destroy(); }
	Operation(const Operation &) = delete;
	Operation &operator=(const Operation &) = delete;
	const MutationRequest &request() const override { return script_.handle.promise().request; }
	void submitted() override {
		if (done() || in_flight_)
			throw std::logic_error("history write already submitted");
		in_flight_ = true;
	}
	bool complete(MutationCompletion completion) noexcept override {
		if (!in_flight_ || completion.token != request().token)
			return false;
		in_flight_ = false;
		script_.handle.promise().result = std::move(completion);
		advance();
		return true;
	}
	bool done() const noexcept override { return script_.handle.done(); }
	bool in_flight() const noexcept override { return in_flight_; }
	kronuz::journal::IO &io() const noexcept override { return *io_; }
	std::exception_ptr error() const noexcept { return script_.handle.promise().error; }

  private:
	void advance() noexcept {
		script_.handle.resume();
		if (!done())
			script_.handle.promise().request.token = {{}, 0, ++step_};
	}
	std::shared_ptr<kronuz::journal::IO> io_;
	std::shared_ptr<const Write> write_;
	std::uint64_t step_ = 0;
	bool in_flight_ = false;
	Script script_;
};
} // namespace

std::size_t file_bytes(const Limits &limits) {
	// "DTABAS01", the namespace (a length and at most 199 bytes), the incarnation, the
	// position and the change count; each change then takes its live footprint.
	constexpr std::size_t header = 8 + 8 + 199 + 16 + 8 + 8;
	if (limits.state_bytes > std::numeric_limits<std::size_t>::max() - header)
		throw std::invalid_argument("history files above any fetch bound");
	return limits.state_bytes + header;
}
std::optional<std::string> serve(kronuz::journal::IO &io, std::string_view name, std::size_t bytes) {
	if (!history_name(name))
		return std::nullopt;
	auto file = io.open_reclaim_candidate_read_only(name);
	if (!file)
		return std::nullopt;
	if (file->size() > bytes)
		throw std::length_error("history file above the fetch bound");
	return read_all(*file);
}
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
	Write write;
	write.groups.push_back(files);
	kronuz::journal::drive_synchronously(*write_operation(io_, std::make_shared<const Write>(std::move(write))));
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

std::shared_ptr<kronuz::journal::IOOperation> write_operation(std::shared_ptr<kronuz::journal::IO> io,
															  std::shared_ptr<const Write> write) {
	std::set<std::string, std::less<>> keep(write->above.begin(), write->above.end());
	if (write->table)
		for (const auto &[name, _] : reached(*write->table))
			keep.insert(name);
	return std::make_shared<Operation>(std::move(io), std::move(write), std::move(keep));
}

History::History(std::shared_ptr<kronuz::journal::IO> io, Limits limits, Fetch fetch)
	: directory_(std::move(io), limits), limits_(limits), fetch_(std::move(fetch)), layout_(limits) {}

std::optional<std::string> History::find(std::string_view name) {
	if (auto planned = planned_.find(name); planned != planned_.end())
		return planned->second;
	if (auto queued = unsynced_.find(name); queued != unsynced_.end())
		return queued->second.bytes;
	return directory_.find(name);
}
std::string History::read(std::string_view name) {
	auto bytes = find(name);
	if (!bytes)
		throw std::runtime_error("absent history file " + std::string(name));
	return std::move(*bytes);
}

// A file with a digest matches it; a seal file without one verifies its own.
Check History::check(const std::optional<Digest> &digest) const {
	return [digest, limits = limits_](std::string_view bytes) {
		if (digest)
			return sha256(bytes) == *digest;
		try {
			decode_seal(bytes, limits);
			return true;
		} catch (const std::invalid_argument &) {
			return false;
		}
	};
}
std::string History::verified(std::string_view name, const std::optional<Digest> &digest) {
	auto good = check(digest);
	if (auto local = find(name); local && good(*local))
		return std::move(*local);
	// A peer holds the same bytes under the same name.
	std::string why;
	if (fetch_) {
		auto fetched = fetched_.find(name);
		if (fetched == fetched_.end())
			throw Wanted{{{std::string(name), std::move(good)}}};
		const auto &bytes = fetched->second.bytes;
		if (bytes && good(*bytes)) {
			// Fetched files are independent: one sync group, before anything derived from them.
			if (!fetched_group_) {
				fetched_group_ = plan_.groups.size();
				plan_.groups.emplace_back();
			}
			plan_.groups[*fetched_group_].emplace_back(name, *bytes);
			planned_[std::string(name)] = *bytes;
			return *bytes;
		}
		why = ": " + fetched->second.why;
	}
	throw std::runtime_error("history file " + std::string(name) + " is missing or rotten and no peer holds it" +
							 why);
}

namespace {
// A table that fails its digest is missing: a peer's copy, or the first bases, replaces it.
std::optional<Table> decoded(const std::optional<std::string> &bytes, const Limits &limits) {
	if (!bytes)
		return std::nullopt;
	try {
		return decode_table(*bytes, limits);
	} catch (const std::invalid_argument &) {
		return std::nullopt;
	}
}
} // namespace
std::optional<Table> History::local_table() { return decoded(find(table_name), limits_); }
std::optional<Table> History::peer_table() {
	if (!fetch_)
		return std::nullopt;
	auto fetched = fetched_.find(table_name);
	if (fetched == fetched_.end())
		throw Wanted{{{std::string(table_name), [limits = limits_](std::string_view bytes) {
						   return decoded(std::string(bytes), limits).has_value();
					   }}}};
	return decoded(fetched->second.bytes, limits_);
}

void History::recover(const State &state) { begin(state, false); }
void History::install(const State &state) { begin(state, true); }
void History::begin(const State &state, bool install) {
	++epoch_;
	ready_ = false;
	durable_ = 0;
	table_ = {};
	failure_ = nullptr;
	layout_ = Layout(limits_);
	install_ = install;
	target_ = state;
	fetched_.clear();
	outstanding_ = 0;
	attempt();
	if (failure_ && !target_)
		std::rethrow_exception(failure_);
}

// Runs the restore against the local files and those fetched so far; when it needs a
// peer's file it fetches every name it found missing and runs again once they land.
void History::attempt() {
	attempting_ = true;
	for (;;) {
		plan_ = {};
		planned_.clear();
		above_.clear();
		fetched_group_.reset();
		std::vector<std::pair<std::string, Check>> wanted;
		try {
			restore();
			break;
		} catch (const Wanted &missing) {
			wanted = missing.names;
		} catch (...) {
			failure_ = std::current_exception();
			plan_ = {};
			planned_.clear();
			fetched_.clear();
			target_.reset();
			break;
		}
		outstanding_ += wanted.size();
		for (auto &[name, good] : wanted)
			fetch_(name, std::move(good), [self = std::weak_ptr<History *>(self_), epoch = epoch_, name](Reply reply) {
				auto alive = self.lock();
				if (!alive)
					return;
				auto &history = **alive;
				if (history.epoch_ != epoch || !history.target_)
					return; // A newer restore superseded this one.
				history.fetched_[name] = std::move(reply);
				if (!--history.outstanding_ && !history.attempting_)
					history.attempt();
			});
		if (outstanding_)
			break;
	}
	attempting_ = false;
}

void History::restore() {
	const auto &state = *target_;
	layout_ = Layout(limits_);
	plan_.temporaries = true;
	std::optional<Table> table;
	if (!state.positions_enabled()) {
		// Replay from before the enabling entry folds none of the seals a table holds.
		if (!install_ && (table = local_table()))
			return adopt(std::move(*table), state);
		return complete(0);
	}
	require(state.seal().index == state.applied_index(),
			install_ ? "a snapshot between seals" : "a checkpoint between seals");
	require(!install_ || bool(fetch_), "an install needs a peer's history");
	if (!install_)
		table = local_table();
	if (!table)
		table = peer_table();
	if (!table) {
		auto fetched = fetched_.find(table_name);
		auto why = fetched == fetched_.end() ? std::string() : ": " + fetched->second.why;
		if (install_)
			throw std::runtime_error("no peer holds a history table" + why);
		// A checkpoint at the enabling entry may precede its table: take the first bases again.
		if (state.seal().ordinal)
			throw std::runtime_error("the history table is missing and no peer holds it" + why);
		auto sealed = state;
		complete(0);
		return seal(sealed);
	}
	adopt(std::move(*table), state);
}

// Adopts a table: every file it reaches verified (or fetched), then the seal files
// through the state's seal. A table past the state's seal already holds the seals
// that replay re-applies; those fold nothing (D6 "Recovery").
void History::adopt(Table table, const State &state) {
	std::vector<std::pair<std::string, Check>> wanted;
	for (const auto &[name, digest] : reached(table))
		try {
			verified(name, digest);
		} catch (Wanted &missing) {
			wanted.push_back(std::move(missing.names.front()));
		}
	if (!wanted.empty())
		throw Wanted{std::move(wanted)};
	layout_ = Layout(table, limits_);
	if (table.index < state.seal().index)
		chain(state, table);
	else
		require(table.index > state.seal().index || table.ordinal == state.seal().ordinal,
				"the history table and the checkpoint disagree");
	// Every file the table reaches is durable before the table that names it.
	stage({{std::string(table_name), encode(table)}});
	plan_.table = std::move(table);
	plan_.above = above_;
	complete(layout_.table().index);
}

// Replays the seal files above the table through the state's seal, newest named first.
void History::chain(const State &state, Table &durable) {
	std::vector<std::string> seals;
	for (auto index = state.seal().index; index > layout_.table().index;) {
		auto bytes = verified(seal_name(index), std::nullopt);
		index = decode_seal(bytes, limits_).previous;
		seals.push_back(std::move(bytes));
	}
	for (auto it = seals.rbegin(); it != seals.rend(); ++it) {
		auto step = layout_.replay(*it, *this);
		stage(step.written); // Rebuilds an output a crash left missing.
		kept(step);
		if (step.table) {
			stage({{std::string(table_name), encode(*step.table)}});
			durable = std::move(*step.table);
		}
	}
	require(layout_.table().index == state.seal().index && layout_.table().ordinal == state.seal().ordinal,
			"the seal chain does not reach the checkpoint");
}

// The collector keeps every seal file above the durable table and its outputs.
void History::kept(const Step &step) {
	if (step.table) {
		above_.clear();
		return;
	}
	above_.push_back(seal_name(step.seal.index));
	for (const auto &o : step.seal.outputs)
		above_.push_back(o.name());
}

// Queues the restore's writes; the layout is ready for seals at once.
void History::complete(std::uint64_t durable) {
	plan_.durable = durable;
	plan_.epoch = epoch_;
	queue(std::move(plan_));
	plan_ = {};
	planned_.clear();
	fetched_.clear();
	target_.reset();
	failure_ = nullptr;
	ready_ = true;
}

void History::stage(Group group) {
	if (group.empty())
		return;
	for (const auto &[name, bytes] : group)
		planned_[name] = bytes;
	plan_.groups.push_back(std::move(group));
}
void History::queue(Write write) {
	for (const auto &group : write.groups)
		for (const auto &[name, bytes] : group) {
			auto &queued = unsynced_[name];
			queued.bytes = bytes;
			++queued.writes;
		}
	writes_.push_back(std::make_shared<const Write>(std::move(write)));
}
void History::release(const Write &write) noexcept {
	for (const auto &group : write.groups)
		for (const auto &[name, _] : group)
			if (auto queued = unsynced_.find(name); queued != unsynced_.end() && !--queued->second.writes)
				unsynced_.erase(queued);
}

void History::seal(const State &sealed) {
	require(ready_, "history unavailable");
	if (sealed.applied_index() <= layout_.table().index)
		return; // The adopted table already holds this seal.
	try {
		auto step = layout_.seal(sealed, *this);
		Write write;
		write.groups.push_back({{seal_name(step.seal.index), std::move(step.bytes)}}); // In seal order.
		if (!step.written.empty())
			write.groups.push_back(std::move(step.written));
		kept(step);
		if (step.table) {
			write.groups.push_back({{std::string(table_name), encode(*step.table)}});
			write.table = std::move(step.table);
		}
		write.durable = step.seal.index;
		write.epoch = epoch_;
		queue(std::move(write));
	} catch (...) {
		ready_ = false;
		throw;
	}
}

void History::abandon() noexcept {
	++epoch_;
	ready_ = false;
	target_.reset();
	fetched_.clear();
	outstanding_ = 0;
}
std::shared_ptr<kronuz::journal::IOOperation> History::take_io_operation() {
	if (operation_ || writes_.empty())
		return nullptr;
	return operation_ = write_operation(directory_.io(), writes_.front());
}
void History::settle() {
	if (!operation_ || !operation_->done())
		return;
	auto error = static_cast<const Operation &>(*operation_).error();
	operation_.reset();
	auto write = std::move(writes_.front());
	writes_.pop_front();
	release(*write);
	if (error)
		return stall(error);
	if (write->epoch != epoch_)
		return;
	durable_ = std::max(durable_, write->durable);
	if (write->table)
		table_ = *write->table;
}
// A failed write stalls history: the queued writes after it depend on it and never run.
void History::stall(std::exception_ptr error) noexcept {
	ready_ = false;
	failure_ = std::move(error);
	while (!writes_.empty() && !(operation_ && writes_.size() == 1)) {
		release(*writes_.back());
		writes_.pop_back();
	}
}
} // namespace detent::authority::history
