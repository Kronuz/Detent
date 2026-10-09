#include "detent_distribution/framing.hh"
#include <algorithm>
#include <cstring>
#include <sha256.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
namespace detent::distribution {
namespace {
void require(bool value, const char *message) {
	if (!value)
		throw std::invalid_argument(message);
}
} // namespace
struct OwnedFragmentEncoder::Impl {
	UpdateKind kind;
	Identity update;
	std::uint64_t total, offset = 0;
	bool compress, final = false, active = false, ready = false, terminal = false, failed = false;
	std::shared_ptr<StorageLedger> account;
	PagedStorage workspace;
	void *workspace_data = nullptr;
	std::size_t workspace_bytes = 0;
	ZSTD_CCtx *context = nullptr;
	std::shared_ptr<const OwnedBuffer> input;
	std::shared_ptr<OwnedBuffer> output;
	SHA256 hash;
	std::size_t credit = 0, position = 0, payload_size = 0, disposal = 0;
	unsigned phase = 0;
	Impl(UpdateKind type, Identity id, std::uint64_t length, bool coding,
		 std::shared_ptr<StorageLedger> ledger)
		: kind(type), update(id), total(length), compress(coding), account(std::move(ledger)),
		  workspace(*account) {
		require(kind == UpdateKind::Snapshot || kind == UpdateKind::Delta || kind == UpdateKind::Progress,
				"invalid owned frame kind");
		require(update != Identity{}, "empty owned frame identity");
		output = make_owned_buffer(account, OwnedFragmentEncoder::maximum_wire_bytes());
		if (compress) {
			const auto parameters = ZSTD_getCParams(3, fragment_bytes, 0);
			workspace_bytes = ZSTD_estimateCCtxSize_usingCParams(parameters);
			if (ZSTD_isError(workspace_bytes))
				throw std::runtime_error("compression workspace estimate failed");
			workspace_data = workspace.allocate(workspace_bytes, 8);
		}
	}
	bool pay(std::size_t bytes, WorkBudget &budget) {
		if (!budget.tokens)
			return false;
		if (!credit)
			credit = bytes;
		const auto n = std::min(credit, budget.bytes);
		credit -= n;
		budget.bytes -= n;
		if (credit)
			return false;
		--budget.tokens;
		return true;
	}
	void header() {
		auto bytes = output->mutable_bytes();
		std::memcpy(bytes.data(), "DTDIST01", 8);
		bytes[8] = 1;
		bytes[9] = static_cast<char>(kind);
		auto put = [&](std::size_t where, std::uint64_t value, unsigned width) {
			for (unsigned i = width; i; --i)
				bytes[where++] = static_cast<char>(value >> ((i - 1) * 8));
		};
		put(10, (compress ? 1 : 0) | (final ? 2 : 0), 2);
		put(12, payload_size, 4);
		put(16, input->size(), 4);
		std::memcpy(bytes.data() + 24, update.data(), update.size());
		put(40, offset, 8);
		put(48, total, 8);
		hash.reset();
		hash.add(bytes.data(), 64);
		position = frame_header_bytes;
	}
};
OwnedFragmentEncoder::OwnedFragmentEncoder(UpdateKind kind, Identity update, std::uint64_t total,
										   bool compress, std::shared_ptr<StorageLedger> account)
	: impl_([&] {
		  if (!account)
			  throw std::invalid_argument("owned frame account missing");
		  return make_storage_owner<Impl>(account, std::malloc, std::free, kind, update, total, compress,
										  account);
	  }()) {}
OwnedFragmentEncoder::~OwnedFragmentEncoder() = default;
std::size_t OwnedFragmentEncoder::maximum_wire_bytes() noexcept {
	return frame_header_bytes + ZSTD_compressBound(fragment_bytes);
}
void OwnedFragmentEncoder::begin(std::shared_ptr<const OwnedBuffer> body, bool final) {
	auto &p = *impl_;
	require(!p.failed && !p.active && !p.ready && !p.terminal, "owned frame encoder busy or terminal");
	require(body && body->sealed() && body->account() == p.account && body->size() <= fragment_bytes &&
				p.offset <= p.total && body->size() <= p.total - p.offset &&
				final == (body->size() == p.total - p.offset) && (final || !body->empty()),
			"invalid owned fragment input");
	if (!p.output)
		p.output = make_owned_buffer(p.account, maximum_wire_bytes());
	p.input = std::move(body);
	p.final = final;
	p.active = true;
	p.phase = 0;
	p.position = p.credit = p.payload_size = 0;
}
void OwnedFragmentEncoder::step(WorkBudget &budget) {
	auto &p = *impl_;
	require(!p.failed, "owned frame encoder inactive");
	if (!p.active || p.ready)
		return;
	try {
		while (true) {
			if (p.phase == 0) {
				const auto extent = p.compress ? maximum_wire_bytes() : frame_header_bytes;
				const auto n = std::min(extent - p.output->size(), budget.bytes);
				p.output->resize(p.output->size() + n);
				budget.bytes -= n;
				if (p.output->size() != extent)
					return;
				p.phase = p.compress ? 1 : 2;
				continue;
			}
			if (p.phase == 1) {
				if (!p.pay(p.workspace_bytes + p.input->size(), budget))
					return;
				if (!p.context) {
					p.context = ZSTD_initStaticCCtx(p.workspace_data, p.workspace_bytes);
					if (!p.context)
						throw std::runtime_error("static compression workspace invalid");
					const auto status =
						ZSTD_CCtx_setCParams(p.context, ZSTD_getCParams(3, fragment_bytes, 0));
					if (ZSTD_isError(status))
						throw std::runtime_error("static compression parameters invalid");
				}
				const auto reset = ZSTD_CCtx_reset(p.context, ZSTD_reset_session_only);
				if (ZSTD_isError(reset))
					throw std::runtime_error("static compression reset failed");
				auto bytes = p.output->mutable_bytes();
				p.payload_size =
					ZSTD_compress2(p.context, bytes.data() + frame_header_bytes,
								   bytes.size() - frame_header_bytes, p.input->data(), p.input->size());
				if (ZSTD_isError(p.payload_size))
					throw std::runtime_error("owned fragment compression failed");
				p.phase = 3;
				continue;
			}
			if (p.phase == 2) {
				const auto n = std::min(p.input->size() - p.position, budget.bytes);
				if (n) {
					p.output->append(p.input->view().substr(p.position, n));
					p.position += n;
					budget.bytes -= n;
				}
				if (p.position != p.input->size())
					return;
				p.payload_size = p.input->size();
				p.phase = 3;
				continue;
			}
			if (p.phase == 3) {
				if (!p.pay(frame_header_bytes, budget))
					return;
				p.header();
				p.phase = 4;
				continue;
			}
			if (p.phase == 4) {
				const auto extent = frame_header_bytes + p.payload_size;
				const auto n = std::min(extent - p.position, budget.bytes);
				if (n) {
					p.hash.add(p.output->data() + p.position, n);
					p.position += n;
					budget.bytes -= n;
				}
				if (p.position != extent)
					return;
				p.phase = 5;
				continue;
			}
			if (!p.pay(SHA256::HashBytes, budget))
				return;
			unsigned char digest[SHA256::HashBytes];
			p.hash.getHash(digest);
			std::memcpy(p.output->mutable_bytes().data() + 64, digest, sizeof(digest));
			p.output->seal_initialized_prefix(frame_header_bytes + p.payload_size);
			p.ready = true;
			return;
		}
	} catch (...) {
		p.failed = true;
		throw;
	}
}
bool OwnedFragmentEncoder::ready() const noexcept { return impl_ && impl_->ready && !impl_->failed; }
std::shared_ptr<const OwnedBuffer> OwnedFragmentEncoder::take() {
	require(ready(), "owned fragment not ready");
	auto &p = *impl_;
	p.offset += p.input->size();
	p.terminal = p.final;
	p.active = p.ready = false;
	p.input.reset();
	return std::exchange(p.output, {});
}
bool OwnedFragmentEncoder::discard(WorkBudget &budget) noexcept {
	auto &p = *impl_;
	p.failed = true;
	p.ready = false;
	p.context = nullptr;
	auto release = [&](auto &owner) {
		if (!owner)
			return true;
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = owner->storage_capacity();
		const auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		owner.reset();
		return true;
	};
	if (!release(p.input) || !release(p.output))
		return false;
	while (const auto capacity = p.workspace.front_capacity()) {
		if (!budget.tokens)
			return false;
		if (!p.disposal)
			p.disposal = capacity;
		const auto n = std::min(p.disposal, budget.bytes);
		p.disposal -= n;
		budget.bytes -= n;
		if (p.disposal)
			return false;
		--budget.tokens;
		p.workspace.release_one();
	}
	return true;
}
} // namespace detent::distribution
