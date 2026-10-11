#include "paged_delta_leaves.hh"
namespace detent::local::detail {
namespace d = distribution;
namespace p = paged;
namespace {
p::Override delta_override(const void *context, std::uint64_t ordinal, std::uint32_t index) {
	const auto &source = *static_cast<const d::ValidatedDelta *>(context);
	const auto &definition = std::get<d::WireBoolDef>(*source.mutation(ordinal).value);
	const auto &row = source.overrides(definition)[index];
	const auto *rollout = std::get_if<GroupRollout>(&row.second);
	return {row.first, rollout != nullptr,
			rollout ? rollout->rollout_pct : static_cast<std::uint8_t>(std::get<bool>(row.second))};
}
std::shared_ptr<d::StorageLedger> account(const std::shared_ptr<const d::ValidatedDelta> &source) {
	if (!source)
		throw std::invalid_argument("missing paged delta source");
	return source->storage_account();
}
} // namespace
struct DeltaLeaves::Impl {
	struct Namespace {
		std::array<char, 198> text{};
		std::size_t length = 0;
		std::uint64_t original = 0, revision = 0;
		std::string_view name() const { return {text.data(), length}; }
	};
	enum class Phase { Namespaces, Collect, Sort, Watermarks, Definitions, Body, Ready, Release, Done };
	std::shared_ptr<const d::ValidatedDelta> source;
	d::PagedStorage arena;
	d::StableRows<Namespace> namespaces;
	DeltaIndex index;
	std::optional<PagedCursor> cursor;
	std::optional<LeafBody> body;
	std::shared_ptr<const d::OwnedBuffer> bytes;
	std::array<char, 202> key{};
	std::size_t length = 0, charge = 0;
	std::uint64_t transaction = 0, touched = 0, position = 0;
	p::Limits limits;
	Phase phase = Phase::Namespaces, after = Phase::Watermarks;
	bool failed = false, discarding = false;
	Impl(const RecoveredGeneration &base, std::shared_ptr<const d::ValidatedDelta> delta, p::Limits bounds)
		: source(std::move(delta)), arena(*account(source), 8192), namespaces(arena), index(arena, *source),
		  cursor(std::in_place, account(source), base, p::PageKind::Namespace), limits(bounds) {
		if (base.descriptor().cursor != source->base() || base.descriptor().profile != source->profile() ||
			(source->kind() != d::UpdateKind::Delta && source->kind() != d::UpdateKind::Progress))
			throw std::invalid_argument("paged delta does not match recovered base");
	}
	bool grant(std::size_t n, ValidationBudget &budget) {
		if (!budget.rows)
			return false;
		if (!charge)
			charge = n;
		auto count = std::min(charge, budget.bytes);
		charge -= count;
		budget.bytes -= count;
		if (charge)
			return false;
		--budget.rows;
		return true;
	}
	std::uint64_t find(std::string_view name) const {
		std::uint64_t low = 0, high = namespaces.size();
		while (low < high) {
			auto middle = low + (high - low) / 2;
			if (namespaces[middle].name() < name)
				low = middle + 1;
			else
				high = middle;
		}
		if (low == namespaces.size() || namespaces[low].name() != name)
			throw std::invalid_argument("delta namespace outside recovered scope");
		return low;
	}
	void collect() {
		if (transaction == source->transactions().size()) {
			index.seal();
			phase = Phase::Sort;
			position = 0;
			return;
		}
		const auto &tx = source->transactions()[transaction];
		if (touched < tx.touched.count) {
			const auto &value = source->touched(tx.touched.start + touched++);
			auto &ns = namespaces[find(value.name)];
			if (value.revision < ns.revision)
				throw std::invalid_argument("decreasing namespace watermark needs full validation");
			ns.revision = value.revision;
		} else if (position < tx.mutations.count) {
			const auto ordinal = tx.mutations.start + position++;
			const auto &mutation = source->mutation(ordinal);
			if (mutation.namespace_index >= namespaces.size())
				throw std::invalid_argument("delta namespace index outside recovered scope");
			const auto &ns = namespaces[mutation.namespace_index];
			if (mutation.key.size() <= ns.length || mutation.key.substr(0, ns.length) != ns.name() ||
				mutation.key[ns.length] != ':' || (mutation.value && tx.revision > ns.revision))
				throw std::invalid_argument("delta definition outside namespace watermark");
			index.append(ordinal, tx.revision);
		} else {
			++transaction;
			touched = position = 0;
		}
	}
};
DeltaLeaves::DeltaLeaves(const RecoveredGeneration &base, std::shared_ptr<const d::ValidatedDelta> source,
						 p::Limits limits)
	: impl_(d::make_storage_owner<Impl>(account(source), std::malloc, std::free, base, source, limits)) {}
DeltaLeaves::~DeltaLeaves() = default;
DeltaLeaves::DeltaLeaves(DeltaLeaves &&) noexcept = default;
void DeltaLeaves::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed || impl_->discarding)
		throw std::logic_error("inactive paged delta leaves");
	auto &s = *impl_;
	try {
		while (s.phase != Impl::Phase::Done && s.phase != Impl::Phase::Ready) {
			switch (s.phase) {
			case Impl::Phase::Namespaces:
				s.cursor->step(budget);
				if (s.cursor->done()) {
					s.phase = Impl::Phase::Collect;
					break;
				}
				if (!s.cursor->ready() || !s.grant(256, budget))
					return;
				{
					const auto leaf = s.cursor->leaf();
					const auto key = p::decode_key(leaf.key);
					if (key.name_space.size() > 198 || s.namespaces.size() >= s.limits.namespaces ||
						(s.namespaces.size() && s.namespaces.back().name() >= key.name_space))
						throw std::length_error("paged delta namespace limit");
					Impl::Namespace row;
					row.length = key.name_space.size();
					std::copy(key.name_space.begin(), key.name_space.end(), row.text.begin());
					row.original = row.revision = read_integer(leaf.body, 0, 8);
					s.namespaces.push_back(row);
					s.cursor->consume();
				}
				break;
			case Impl::Phase::Collect:
				if (!s.grant(256, budget))
					return;
				s.collect();
				break;
			case Impl::Phase::Sort:
				s.index.step(budget);
				if (!s.index.done())
					return;
				s.phase = Impl::Phase::Watermarks;
				break;
			case Impl::Phase::Watermarks:
				if (s.position == s.namespaces.size()) {
					s.position = 0;
					s.phase = Impl::Phase::Definitions;
					break;
				}
				if (!s.grant(256, budget))
					return;
				{
					const auto &ns = s.namespaces[s.position++];
					if (ns.revision == ns.original)
						break;
					s.length = p::encode_namespace_key(s.key, ns.name());
					s.body.emplace(account(s.source), ns.revision);
					s.after = Impl::Phase::Watermarks;
					s.phase = Impl::Phase::Body;
				}
				break;
			case Impl::Phase::Definitions:
				if (s.position == s.index.size()) {
					s.phase = Impl::Phase::Done;
					break;
				}
				if (!s.grant(256, budget))
					return;
				{
					const auto entry = s.index[s.position++];
					const auto &mutation = s.source->mutation(entry.ordinal);
					s.length = p::encode_definition_key(s.key, mutation.key);
					s.after = Impl::Phase::Definitions;
					if (!mutation.value) {
						s.phase = Impl::Phase::Ready;
						break;
					}
					p::Definition definition;
					definition.revision = entry.revision;
					if (const auto *boolean = std::get_if<d::WireBoolDef>(&*mutation.value)) {
						definition.boolean = true;
						definition.default_bool = boolean->default_value;
						definition.rollout = boolean->rollout_pct;
						definition.overrides = boolean->overrides.count;
					} else
						definition.default_int = std::get<IntDef>(*mutation.value).default_value;
					s.body.emplace(account(s.source), definition, s.source.get(), entry.ordinal,
								   delta_override, p::page_bytes - p::page_header_bytes - s.length);
					s.phase = Impl::Phase::Body;
				}
				break;
			case Impl::Phase::Body:
				s.body->step(budget);
				if (!s.body->done())
					return;
				s.bytes = s.body->bytes();
				s.phase = Impl::Phase::Ready;
				break;
			case Impl::Phase::Release:
				if (s.body && !s.body->discard(budget))
					return;
				s.body.reset();
				s.phase = s.after;
				break;
			default:
				break;
			}
		}
	} catch (...) {
		s.failed = true;
		throw;
	}
}
bool DeltaLeaves::done() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Done;
}
bool DeltaLeaves::ready() const noexcept {
	return impl_ && !impl_->failed && !impl_->discarding && impl_->phase == Impl::Phase::Ready;
}
ChangedLeaf DeltaLeaves::leaf() const {
	if (!ready())
		throw std::logic_error("paged delta leaf unavailable");
	std::optional<std::span<const char>> bytes;
	if (impl_->bytes)
		bytes = {impl_->bytes->data(), impl_->bytes->size()};
	return {{impl_->key.data(), impl_->length}, bytes};
}
void DeltaLeaves::consume() {
	if (!ready())
		throw std::logic_error("paged delta leaf unavailable");
	impl_->bytes.reset();
	impl_->phase = Impl::Phase::Release;
}
bool DeltaLeaves::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &s = *impl_;
	if (!s.discarding) {
		s.discarding = true;
		s.charge = 0;
	}
	s.bytes.reset();
	if (s.body && !s.body->discard(budget))
		return false;
	s.body.reset();
	if (s.cursor && !s.cursor->discard(budget))
		return false;
	s.cursor.reset();
	while (s.arena.front_capacity()) {
		if (!s.grant(s.arena.front_capacity(), budget))
			return false;
		s.arena.release_one();
	}
	if (!budget.rows)
		return false;
	--budget.rows;
	impl_.reset();
	return true;
}
} // namespace detent::local::detail
