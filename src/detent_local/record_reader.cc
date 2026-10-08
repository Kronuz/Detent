#include "detent_local/record_reader.hh"
#include <algorithm>

namespace detent::local {
namespace c = kronuz::io::completion;
void RecordReader::start(std::string_view name, std::size_t length, std::uint64_t trusted_owner,
						 DirectoryIdentity directory) {
	if (submitted_ || status_ == RecordReadStatus::Reading)
		throw std::logic_error("record read still active");
	if (name.empty() || name.size() >= name_.size() || name == "." || name == ".." ||
		name.find('/') != name.npos || name.find('\0') != name.npos || !length || length > capacity)
		throw std::invalid_argument("invalid bounded record read");
	std::copy(name.begin(), name.end(), name_.begin());
	name_size_ = name.size();
	length_ = length;
	offset_ = 0;
	trusted_owner_ = trusted_owner;
	directory_ = directory;
	initial_ = {};
	token_ = {};
	error_ = {};
	prepared_ = submitted_ = cancelled_ = false;
	phase_ = Phase::Named;
	status_ = RecordReadStatus::Reading;
}
c::Request RecordReader::prepare(c::Token token, std::shared_ptr<c::File> file) {
	if (status_ != RecordReadStatus::Reading || submitted_ || !file)
		throw std::logic_error("invalid record read preparation");
	if (prepared_ && token != token_)
		throw std::logic_error("record read token changed before submission");
	if (!prepared_ && phase_ != Phase::Named &&
		(token.owner != token_.owner || token.operation != token_.operation || token.step <= token_.step))
		throw std::logic_error("record read requires a fresh step in the same operation");
	token_ = token;
	prepared_ = true;
	c::Request request;
	request.token = token;
	request.file = std::move(file);
	switch (phase_) {
	case Phase::Named:
	case Phase::VerifyNamed:
		request.kind = c::Kind::InspectEntry;
		request.source = {name_.data(), name_size_};
		break;
	case Phase::Held:
	case Phase::VerifyHeld:
		request.kind = c::Kind::InspectFile;
		break;
	case Phase::Read:
		request.kind = c::Kind::Read;
		request.offset = offset_;
		request.destination_bytes = std::span(bytes_).subspan(offset_, length_ - offset_);
		break;
	}
	return request;
}
void RecordReader::submitted() {
	if (status_ != RecordReadStatus::Reading || !prepared_ || submitted_)
		throw std::logic_error("invalid record read submission");
	submitted_ = true;
}
bool RecordReader::safe(const c::EntryInspection &entry) const noexcept {
	return entry.footprint.kind == c::EntryKind::Regular && entry.device == directory_.device &&
		   entry.owner == trusted_owner_ && entry.links == 1 && !(entry.permissions & 0022) &&
		   entry.footprint.logical_bytes == length_;
}
bool RecordReader::unchanged(const c::EntryInspection &entry) const noexcept {
	return safe(entry) && entry.device == initial_.device && entry.inode == initial_.inode &&
		   entry.permissions == initial_.permissions;
}
bool RecordReader::complete(c::Result result) noexcept {
	if (!submitted_ || result.token != token_)
		return false;
	submitted_ = prepared_ = false;
	if (result.error) {
		error_ = std::move(result.error);
		status_ = RecordReadStatus::Failed;
		return true;
	}
	if (cancelled_) {
		status_ = RecordReadStatus::Cancelled;
		return true;
	}
	try {
		switch (phase_) {
		case Phase::Named:
			if (!result.inspection) {
				status_ = RecordReadStatus::Missing;
			} else if (!safe(*result.inspection)) {
				status_ = RecordReadStatus::Unsafe;
			} else {
				initial_ = *result.inspection;
				phase_ = Phase::Held;
			}
			break;
		case Phase::Held:
		case Phase::VerifyHeld:
		case Phase::VerifyNamed:
			if (!result.inspection || !unchanged(*result.inspection)) {
				status_ = RecordReadStatus::Changed;
			} else if (phase_ == Phase::Held) {
				phase_ = Phase::Read;
			} else if (phase_ == Phase::VerifyHeld) {
				phase_ = Phase::VerifyNamed;
			} else {
				status_ = RecordReadStatus::Ready;
			}
			break;
		case Phase::Read:
			if (!result.count || result.count > length_ - offset_)
				throw std::runtime_error("invalid or incomplete immutable record read");
			offset_ += result.count;
			if (offset_ == length_)
				phase_ = Phase::VerifyHeld;
			break;
		}
	} catch (...) {
		error_ = std::current_exception();
		status_ = RecordReadStatus::Failed;
	}
	return true;
}
void RecordReader::cancel() noexcept {
	if (status_ != RecordReadStatus::Reading)
		return;
	cancelled_ = true;
	if (!submitted_) {
		prepared_ = false;
		status_ = RecordReadStatus::Cancelled;
	}
}
std::span<const char> RecordReader::bytes() const {
	if (status_ != RecordReadStatus::Ready)
		throw std::logic_error("record read not validated");
	return {bytes_.data(), length_};
}
} // namespace detent::local
