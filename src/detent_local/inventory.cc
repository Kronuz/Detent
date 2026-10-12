#include "detent_local/inventory.hh"
#include "names.hh"
#include "segment_names.hh"
#include <limits>

namespace detent::local {
namespace c = inventory_completion;
struct InventoryOperation::State {
	std::shared_ptr<StorageSession> session;
	std::unique_ptr<c::DirectoryCursor> cursor;
	std::shared_ptr<CollectorControls> controls;
	CollectorControls::Claim claim;
	std::array<char, 256> name{};
	std::size_t name_size = 0;
	InventoryReport report;
	std::uint64_t work_limit, trusted_owner;
	InventoryStatus status = InventoryStatus::Scanning;
	std::exception_ptr error;
	bool submitted = false, cancelled = false, cursor_settled = false, cursor_open = false;
	State(std::shared_ptr<StorageSession> owner, std::shared_ptr<CollectorControls> collector = {})
		: session(std::move(owner)), controls(std::move(collector)), work_limit(0), trusted_owner(0) {}
	c::DirectoryCursor *cursor_handle() const noexcept {
		return controls ? &controls->cursor() : cursor.get();
	}
};
InventoryOperation::InventoryOperation(std::shared_ptr<StorageSession> session, std::uint64_t work_limit,
									   std::uint64_t trusted_owner) {
	if (!session)
		throw std::invalid_argument("inventory session missing");
	const auto caps = session->backend()->managed_capabilities();
	if (!caps.caller_scan || !caps.entry_inspection || !caps.cursor_close)
		throw std::invalid_argument("inventory scan/inspection/close capabilities missing");
	state_ = std::allocate_shared<State>(c::OwnedAllocator<State>(session->allocation_context()), session);
	initialize_pass(work_limit, trusted_owner);
}
InventoryOperation::InventoryOperation(std::shared_ptr<CollectorControls> controls, std::uint64_t work_limit,
									   std::uint64_t trusted_owner) {
	if (!controls)
		throw std::invalid_argument("inventory collector controls missing");
	const auto &session = controls->session();
	state_ = std::allocate_shared<State>(c::OwnedAllocator<State>(session->allocation_context()), session,
										 controls);
	initialize_pass(work_limit, trusted_owner);
}
void InventoryOperation::initialize_pass(std::uint64_t work_limit, std::uint64_t trusted_owner) {
	if (work_limit > (std::numeric_limits<std::uint64_t>::max() - 3) / 2)
		throw std::length_error("inventory work exceeds completion token space");
	// Obtain a token before claiming controls; neither can leave partial IO.
	const auto token = state_->session->next_operation();
	CollectorControls::Claim claim;
	if (state_->controls)
		claim = state_->controls->claim(CollectorUse::Census);
	state_->claim = claim;
	state_->work_limit = work_limit;
	state_->trusted_owner = trusted_owner;
	state_->report = {};
	state_->name_size = 0;
	state_->error = {};
	state_->cancelled = state_->cursor_settled = state_->cursor_open = false;
	state_->status = InventoryStatus::Scanning;
	request_ = {};
	request_.token = token;
	next(state_->controls ? c::Kind::ScanInto : c::Kind::Scan);
}
void InventoryOperation::restart(std::uint64_t work_limit, std::uint64_t trusted_owner) {
	if (!state_->controls || !done() || in_flight())
		throw std::logic_error("inventory reuse requires settled reusable controls");
	initialize_pass(work_limit, trusted_owner);
}
InventoryOperation::~InventoryOperation() = default;
const c::Request &InventoryOperation::request() const { return request_; }
bool InventoryOperation::done() const noexcept { return state_->status != InventoryStatus::Scanning; }
bool InventoryOperation::in_flight() const noexcept { return state_->submitted; }
c::IO &InventoryOperation::io() const noexcept { return *state_->session->backend(); }
InventoryStatus InventoryOperation::status() const noexcept { return state_->status; }
std::exception_ptr InventoryOperation::error() const noexcept { return state_->error; }
const InventoryReport &InventoryOperation::report() const {
	if (status() != InventoryStatus::Ready)
		throw std::logic_error("inventory accounting is incomplete");
	return state_->report;
}
void InventoryOperation::next(c::Kind kind) {
	if (request_.token.step == std::numeric_limits<std::uint64_t>::max())
		throw std::overflow_error("inventory step identity exhausted");
	const auto token = request_.token;
	request_ = {};
	request_.token = token;
	++request_.token.step;
	request_.kind = kind;
	if (kind == c::Kind::NextInto || kind == c::Kind::CloseCursor || kind == c::Kind::ScanInto)
		request_.cursor = std::shared_ptr<c::DirectoryCursor>(state_, state_->cursor_handle());
	if (kind == c::Kind::NextInto)
		request_.destination_bytes = state_->name;
	if (kind == c::Kind::InspectEntry)
		request_.source = {state_->name.data(), state_->name_size};
}
void InventoryOperation::submitted() {
	if (done() || state_->submitted)
		throw std::logic_error("invalid inventory submission");
	state_->submitted = true;
}
void InventoryOperation::finish_or_close() {
	if (state_->cursor_open && !state_->cursor_settled) {
		if (request_.kind != c::Kind::CloseCursor)
			next(c::Kind::CloseCursor);
		return;
	}
	state_->status = state_->error		 ? InventoryStatus::Failed
					 : state_->cancelled ? InventoryStatus::Cancelled
										 : InventoryStatus::Ready;
	request_.cursor.reset();
	if (state_->controls && state_->claim.serial) {
		state_->controls->release(state_->claim);
		state_->claim = {};
	}
}
void InventoryOperation::cancel() noexcept {
	if (done())
		return;
	state_->cancelled = true;
	if (!state_->submitted) {
		try {
			finish_or_close();
		} catch (...) {
			if (!state_->error)
				state_->error = std::current_exception();
			state_->status = InventoryStatus::Failed;
		}
	}
}
void InventoryOperation::observe(const c::EntryInspection &entry) {
	auto &report = state_->report;
	auto add = [](std::uint64_t &value, std::uint64_t amount) {
		if (amount > std::numeric_limits<std::uint64_t>::max() - value)
			throw std::overflow_error("inventory accounting overflow");
		value += amount;
	};
	add(report.usage.bytes, entry.footprint.logical_bytes);
	add(report.usage.entries, 1);
	add(report.inspected, 1);
	const std::string_view name{state_->name.data(), state_->name_size};
	const auto parsed = detail::parse_generation_name(name);
	const auto segment = detail::parse_segment_name(name);
	if (parsed) {
		if (parsed->kind == detail::GenerationFile::Lease)
			add(report.usage.generations, 1);
		else
			add(report.canonical_payloads, 1);
	}
	if (segment && segment->kind == detail::SegmentFile::Payload)
		add(report.canonical_payloads, 1);
	const bool directory = entry.footprint.kind == c::EntryKind::Directory;
	const bool unsafe = entry.footprint.kind != c::EntryKind::Regular || entry.links != 1 ||
						entry.owner != state_->trusted_owner || (entry.permissions & 0022);
	const bool foreign =
		!parsed && !segment && name != "owner.lock" && name != "storage.meta" && name != "selected";
	if (unsafe || foreign) {
		add(report.quarantined, 1);
		if (report.sample_count < report.samples.size()) {
			auto &sample = report.samples[report.sample_count++];
			sample.size = name.size();
			std::copy(name.begin(), name.end(), sample.name.begin());
			sample.issue = directory ? InventoryIssue::Directory
						   : unsafe	 ? InventoryIssue::Unsafe
									 : InventoryIssue::Foreign;
		}
	}
	if (directory)
		throw std::runtime_error("inventory encountered an unexpected directory");
}
bool InventoryOperation::complete(c::Result result) noexcept {
	if (!state_->submitted || result.token != request_.token)
		return false;
	state_->submitted = false;
	const auto kind = request_.kind;
	// A returned resource belongs to this authentic original even on failure.
	if (kind == c::Kind::Scan && result.cursor) {
		state_->cursor = std::move(result.cursor);
		state_->cursor_open = true;
	}
	// Into failure leaves its preadmitted destination closed by contract.
	if (kind == c::Kind::ScanInto && !result.error)
		state_->cursor_open = true;
	if (result.error && !state_->error)
		state_->error = result.error;
	try {
		if (kind == c::Kind::CloseCursor) {
			state_->cursor_settled = true;
			state_->cursor_open = false;
			finish_or_close();
			return true;
		}
		if (state_->error || state_->cancelled) {
			finish_or_close();
			return true;
		}
		switch (kind) {
		case c::Kind::Scan:
		case c::Kind::ScanInto:
			if (!state_->cursor_handle())
				throw std::runtime_error("inventory scan returned no cursor");
			next(c::Kind::NextInto);
			break;
		case c::Kind::NextInto: {
			if (!result.count) {
				finish_or_close();
				break;
			}
			if (result.count >= state_->name.size())
				throw std::runtime_error("inventory scan exceeded name scratch");
			if (state_->report.inspected >= state_->work_limit)
				throw std::length_error("inventory scan work exhausted");
			const std::string_view name{state_->name.data(), result.count};
			if (name == "." || name == ".." || name.find('/') != name.npos || name.find('\0') != name.npos)
				throw std::runtime_error("inventory returned an invalid basename");
			state_->name_size = result.count;
			next(c::Kind::InspectEntry);
			break;
		}
		case c::Kind::InspectEntry:
			if (!result.inspection)
				throw std::runtime_error("entry disappeared during quiescent inventory");
			observe(*result.inspection);
			next(c::Kind::NextInto);
			break;
		default:
			throw std::logic_error("invalid inventory phase");
		}
	} catch (...) {
		if (!state_->error)
			state_->error = std::current_exception();
		try {
			finish_or_close();
		} catch (...) {
			state_->status = InventoryStatus::Failed;
		}
	}
	return true;
}
std::shared_ptr<InventoryOperation> make_inventory_operation(std::shared_ptr<StorageSession> session,
															 std::uint64_t work_limit,
															 std::uint64_t trusted_owner) {
	if (!session)
		throw std::invalid_argument("inventory session missing");
	return std::allocate_shared<InventoryOperation>(
		c::OwnedAllocator<InventoryOperation>(session->allocation_context()), session, work_limit,
		trusted_owner);
}
std::shared_ptr<InventoryOperation> make_inventory_operation(std::shared_ptr<CollectorControls> controls,
															 std::uint64_t work_limit,
															 std::uint64_t trusted_owner) {
	if (!controls)
		throw std::invalid_argument("inventory collector missing");
	return std::allocate_shared<InventoryOperation>(
		c::OwnedAllocator<InventoryOperation>(controls->session()->allocation_context()), controls,
		work_limit, trusted_owner);
}
} // namespace detent::local
