#include "detent_distribution/frame_input.hh"
#include "detent/scope.hh"
#include <algorithm>
#include <numeric>

namespace detent::distribution {
namespace {
void require(bool condition, const char *message) {
	if (!condition)
		throw std::invalid_argument(message);
}
template <class Storage> WireDefinition wire(Storage &storage, const KnobDef &value) {
	WireDefinition out;
	out.default_value = value.default_value;
	out.rollout_pct = value.rollout_pct;
	out.rollout_value = value.rollout_value;
	out.overrides = {storage.overrides.size(), value.overrides.size()};
	for (const auto &[group, override] : value.overrides)
		storage.overrides.push_back({store_text(storage.arena, group), override});
	return out;
}
} // namespace

Cursor frame_cursor(std::string_view name, const NamespaceCursor &cursor) {
	class SHA256 hash;
	constexpr std::string_view domain = "detent.frame-namespace";
	hash.add(domain.data(), domain.size());
	hash.add(name.data(), name.size());
	ScopeIdentity scope{};
	hash.getHash(reinterpret_cast<unsigned char *>(scope.data()));
	return {cursor.cluster, cursor.incarnation, cursor.incarnation, scope, cursor.position};
}

FrameInput::FrameInput(std::string_view name, FrameKind kind, std::uint64_t after, const NamespaceCursor &to,
					   Profile profile, const Limits &limits)
	: name_(name), kind_(kind), after_(after), to_(to), profile_(profile), limits_(detail::admitted_limits(limits)) {
	require(kind == FrameKind::FullCopy || kind == FrameKind::Entries || kind == FrameKind::Blocks,
			"frame input takes a full copy, entries or blocks");
	if (kind == FrameKind::FullCopy) {
		snapshot_.reset(new ValidatedSnapshot(limits_));
		rows_ = std::make_unique<StableRows<Row>>(snapshot_->storage_->arena);
	} else {
		delta_.reset(new ValidatedDelta(limits_));
		rows_ = std::make_unique<StableRows<Row>>(delta_->storage_->arena);
	}
}
FrameInput::~FrameInput() = default;

void FrameInput::piece(PieceKind, std::uint64_t, std::uint64_t) {}

template <class Storage>
void FrameInput::add(Storage &storage, std::string_view key, std::uint64_t revision, const KnobDef *value) {
	if (ordered_ && (!value || (rows_->size() && !((*rows_)[rows_->size() - 1].key < key))))
		ordered_ = false;
	Row row{store_text(storage.arena, key), revision, {}, !value};
	if (value)
		row.definition = wire(storage, *value);
	rows_->push_back(row);
}
void FrameInput::record(std::string_view key, std::uint64_t revision, const KnobDef *value) {
	if (!rows_)
		throw std::logic_error("frame input already taken");
	require(revision > after_ && revision <= to_.position, "frame record outside its frame");
	if (snapshot_)
		add(*snapshot_->storage_, key, revision, value);
	else
		add(*delta_->storage_, key, revision, value);
}
std::size_t FrameInput::bytes() const noexcept {
	return snapshot_ ? snapshot_->storage_capacity() : delta_ ? delta_->storage_capacity() : 0;
}

std::vector<std::uint64_t> FrameInput::winners() const {
	std::vector<std::uint64_t> order(rows_->size());
	std::iota(order.begin(), order.end(), 0);
	if (ordered_)
		return order;
	// Positions only grow through a frame, so a key's last row is its newest.
	std::stable_sort(order.begin(), order.end(),
					 [&](auto a, auto b) { return (*rows_)[a].key < (*rows_)[b].key; });
	std::vector<std::uint64_t> out;
	for (std::size_t i = 0; i < order.size(); ++i)
		if (i + 1 == order.size() || (*rows_)[order[i]].key != (*rows_)[order[i + 1]].key)
			out.push_back(order[i]);
	return out;
}

std::shared_ptr<ValidatedSnapshot> FrameInput::snapshot() {
	if (!snapshot_ || !rows_)
		throw std::logic_error("frame input holds no full copy");
	auto &storage = *snapshot_->storage_;
	snapshot_->cursor_ = frame_cursor(name_, to_);
	snapshot_->namespace_cursor_ = to_;
	snapshot_->profile_ = profile_;
	storage.namespaces.push_back({store_text(storage.arena, name_), to_.position});
	for (const auto index : winners()) {
		const auto &row = (*rows_)[index];
		if (!row.tombstone)
			storage.records.push_back({0, row.key, row.revision, row.definition});
	}
	rows_.reset();
	return std::move(snapshot_);
}

std::shared_ptr<ValidatedDelta> FrameInput::delta() {
	if (!delta_ || !rows_)
		throw std::logic_error("frame input holds no delta");
	auto &storage = *delta_->storage_;
	delta_->base_ = frame_cursor(name_, {to_.cluster, to_.incarnation, after_});
	delta_->target_ = frame_cursor(name_, to_);
	delta_->profile_ = profile_;
	delta_->kind_ = UpdateKind::Delta;
	auto order = winners();
	std::stable_sort(order.begin(), order.end(),
					 [&](auto a, auto b) { return (*rows_)[a].revision < (*rows_)[b].revision; });
	const auto name = store_text(storage.arena, name_);
	auto transaction = [&](std::uint64_t revision) {
		storage.transactions.push_back({revision, {storage.touched.size(), 1}, {storage.mutations.size(), 0}});
		storage.touched.push_back({name, revision});
	};
	for (const auto index : order) {
		const auto &row = (*rows_)[index];
		if (storage.transactions.empty() || storage.transactions.back().revision != row.revision)
			transaction(row.revision);
		storage.mutations.push_back(
			{0, row.key, row.tombstone ? std::nullopt : std::optional<WireDefinition>(row.definition)});
		++storage.transactions.back().mutations.count;
	}
	// The namespace's watermark reaches the target even when its last positions left no record.
	if (to_.position > after_ && (storage.transactions.empty() || storage.transactions.back().revision < to_.position))
		transaction(to_.position);
	rows_.reset();
	return std::move(delta_);
}
} // namespace detent::distribution
