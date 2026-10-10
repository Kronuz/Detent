#include "detent_distribution/snapshot_encoder.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>
namespace detent::distribution {
namespace d = detent::distribution;
namespace m = distribution::messagepack;
struct SnapshotEncoder::Impl {
	static constexpr std::size_t chunk_bytes = 65536;
	std::shared_ptr<SnapshotSource> source;
	d::Limits limits;
	std::shared_ptr<d::OwnedBuffer> output;
	m::InlineBytes<512> pending;
	std::optional<Record> current;
	bool group = false, records_done = false;
	std::uint64_t overrides_remaining = 0;
	std::uint64_t namespaces = 0, records = 0, overrides = 0, strings = 0, emitted = 0, wire_length = 0;
	std::size_t offset = 0, charge = 0, definition_bytes = 0;
	unsigned phase = 0;
	bool ready = false, finished = false, failed = false;
	Impl(std::shared_ptr<SnapshotSource> projection, d::Limits bounds)
		: source(std::move(projection)), limits(std::move(bounds)),
		  output(d::make_owned_buffer(source->storage_account(), chunk_bytes)) {
		if (source->namespace_count() > limits.namespaces)
			throw std::length_error("export scope limit");
		source->reset_records();
	}
	void count_text(std::string_view value) {
		if (value.size() > limits.string_bytes - strings)
			throw std::length_error("export string limit");
		strings += value.size();
	}
	void advance_record() {
		auto next = source->next_record();
		if (next.done && next.record)
			throw std::logic_error("snapshot source terminal record mismatch");
		current = std::move(next.record);
		records_done = next.done;
	}
	void count_wire(const m::InlineBytes<512> &bytes) {
		if (bytes.size() > limits.bytes - wire_length)
			throw std::length_error("export wire byte limit");
		wire_length += bytes.size();
	}
	void namespace_row(m::InlineBytes<512> &out, std::uint64_t ordinal) const {
		m::array(out, 2);
		const auto row = source->name_space(ordinal);
		m::string(out, row.name);
		m::unsigned_integer(out, row.revision);
	}
	static void override_row(m::InlineBytes<512> &out, std::string_view name, const GroupOverride &value) {
		m::array(out, 3);
		m::string(out, name);
		if (auto forced = std::get_if<bool>(&value)) {
			m::unsigned_integer(out, 0);
			out.push_back(*forced ? '\xc3' : '\xc2');
		} else {
			m::unsigned_integer(out, 1);
			m::unsigned_integer(out, std::get<GroupRollout>(value).rollout_pct);
		}
	}
	static void record_row(m::InlineBytes<512> &out, const Record &row) {
		m::array(out, 4);
		m::unsigned_integer(out, row.namespace_index);
		m::string(out, row.key);
		m::unsigned_integer(out, row.revision);
		if (auto flag = std::get_if<WireBoolDef>(&row.definition)) {
			m::array(out, 4);
			m::unsigned_integer(out, 0);
			out.push_back(flag->default_value ? '\xc3' : '\xc2');
			if (flag->rollout_pct)
				m::unsigned_integer(out, *flag->rollout_pct);
			else
				out.push_back('\xc0');
			m::array(out, flag->overrides.count);
		} else {
			m::array(out, 2);
			m::unsigned_integer(out, 1);
			m::signed_integer(out, std::get<IntDef>(row.definition).default_value);
		}
	}
	void count_one() {
		m::InlineBytes<512> encoded;
		if (phase == 0) {
			if (namespaces < source->namespace_count()) {
				count_text(source->name_space(namespaces).name);
				namespace_row(encoded, namespaces++);
				count_wire(encoded);
			} else {
				namespaces = 0;
				phase = 1;
			}
			return;
		}
		if (group) {
			auto entry = source->next_override();
			if (entry) {
				if (!overrides_remaining--)
					throw std::logic_error("snapshot source extra override");
				definition_bytes +=
					9 + entry->first.size() + (std::holds_alternative<bool>(entry->second) ? 1 : 8);
				if (definition_bytes > limits.definition_bytes)
					throw std::length_error("export definition limit");
				count_text(entry->first);
				override_row(encoded, entry->first, entry->second);
				count_wire(encoded);
				++overrides;
			} else {
				if (overrides_remaining)
					throw std::logic_error("snapshot source missing override");
				group = false;
				current.reset();
			}
			return;
		}
		if (!current) {
			if (records_done) {
				header(encoded);
				m::array(encoded, records);
				count_wire(encoded);
				namespaces = 0;
				source->reset_records();
				records_done = false;
				phase = 2;
				return;
			}
			advance_record();
			return;
		}
		if (records == limits.records)
			throw std::length_error("export record limit");
		++records;
		count_text(current->key);
		record_row(encoded, *current);
		count_wire(encoded);
		if (const auto *flag = std::get_if<WireBoolDef>(&current->definition)) {
			if (flag->overrides.count > limits.overrides_per_definition)
				throw std::length_error("export override limit");
			definition_bytes = flag->rollout_pct ? 19 : 11;
			if (definition_bytes > limits.definition_bytes)
				throw std::length_error("export definition limit");
			group = true;
			overrides_remaining = flag->overrides.count;
		} else {
			if (limits.definition_bytes < 9)
				throw std::length_error("export definition limit");
			current.reset();
		}
	}
	void header(m::InlineBytes<512> &out) const {
		m::array(out, 7);
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, 1);
		m::array(out, 2);
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, 1);
		const auto &id = source->cursor();
		m::array(out, 5);
		for (auto *field : {&id.cluster, &id.group, &id.restore_epoch})
			m::string(out, {field->data(), field->size()}, true);
		m::string(out, {id.scope.data(), id.scope.size()}, true);
		m::unsigned_integer(out, id.revision);
		m::array(out, 4);
		m::unsigned_integer(out, source->namespace_count());
		m::unsigned_integer(out, records);
		m::unsigned_integer(out, overrides);
		m::unsigned_integer(out, strings);
		m::array(out, source->namespace_count());
	}
	void emit_one() {
		if (phase == 2) {
			header(pending);
			phase = 3;
			return;
		}
		if (phase == 3) {
			if (namespaces < source->namespace_count())
				namespace_row(pending, namespaces++);
			else {
				m::array(pending, records);
				namespaces = 0;
				phase = 4;
			}
			return;
		}
		if (group) {
			auto entry = source->next_override();
			if (entry) {
				if (!overrides_remaining--)
					throw std::logic_error("snapshot source extra override");
				override_row(pending, entry->first, entry->second);
			} else {
				if (overrides_remaining)
					throw std::logic_error("snapshot source missing override");
				group = false;
				current.reset();
			}
			return;
		}
		if (!current) {
			if (records_done) {
				if (emitted != wire_length)
					throw std::logic_error("export count/emission mismatch");
				finished = true;
				return;
			}
			advance_record();
			return;
		}
		record_row(pending, *current);
		if (auto flag = std::get_if<WireBoolDef>(&current->definition)) {
			group = true;
			overrides_remaining = flag->overrides.count;
		} else
			current.reset();
	}
};
SnapshotEncoder::SnapshotEncoder(std::shared_ptr<SnapshotSource> source, d::Limits limits)
	: impl_([&] {
		  if (!source || !source->storage_account() ||
			  (limits.storage_account && limits.storage_account != source->storage_account()))
			  throw std::invalid_argument("snapshot source account mismatch");
		  return d::make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free, source,
											 limits);
	  }()) {}
SnapshotEncoder::~SnapshotEncoder() = default;
SnapshotEncoder::SnapshotEncoder(SnapshotEncoder &&) noexcept = default;
void SnapshotEncoder::step(d::WorkBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive snapshot export encoder");
	auto &p = *impl_;
	try {
		if (p.finished && !p.ready && p.pending.empty())
			return;
		if (!p.output) {
			if (!budget.tokens || !budget.bytes)
				return;
			--budget.tokens;
			p.output = d::make_owned_buffer(p.source->storage_account(), Impl::chunk_bytes);
		}
		while (!p.ready) {
			if (!p.pending.empty()) {
				const auto room = Impl::chunk_bytes - p.output->size();
				const auto n = std::min({room, p.pending.size() - p.offset, budget.bytes});
				if (!n)
					return;
				if (n > p.limits.bytes - p.emitted)
					throw std::length_error("export wire byte limit");
				p.output->append({p.pending.data() + p.offset, n});
				p.offset += n;
				p.emitted += n;
				budget.bytes -= n;
				if (p.offset == p.pending.size()) {
					p.pending.clear();
					p.offset = 0;
				}
				if (p.output->size() == Impl::chunk_bytes) {
					p.output->seal();
					p.ready = true;
					return;
				}
				continue;
			}
			if (p.finished) {
				if (!p.output->empty()) {
					p.output->seal_prefix();
					p.ready = true;
				}
				return;
			}
			if (!budget.tokens)
				return;
			if (!p.charge)
				p.charge = 512;
			auto n = std::min(p.charge, budget.bytes);
			p.charge -= n;
			budget.bytes -= n;
			if (p.charge)
				return;
			--budget.tokens;
			if (!p.output)
				p.output = d::make_owned_buffer(p.source->storage_account(), Impl::chunk_bytes);
			if (p.phase < 2)
				p.count_one();
			else
				p.emit_one();
		}
	} catch (...) {
		p.failed = true;
		throw;
	}
}
std::uint64_t SnapshotEncoder::encoded_length() const {
	if (!impl_ || impl_->failed || impl_->phase < 2)
		throw std::logic_error("snapshot export count incomplete");
	return impl_->wire_length;
}
bool SnapshotEncoder::counted() const noexcept { return impl_ && !impl_->failed && impl_->phase >= 2; }
bool SnapshotEncoder::done() const noexcept {
	return impl_ && impl_->finished && !impl_->ready && !impl_->failed;
}
bool SnapshotEncoder::chunk_ready() const noexcept { return impl_ && impl_->ready && !impl_->failed; }
std::shared_ptr<const d::OwnedBuffer> SnapshotEncoder::take_chunk() {
	if (!chunk_ready())
		throw std::logic_error("export chunk unavailable");
	impl_->ready = false;
	return std::exchange(impl_->output, {});
}
void SnapshotEncoder::discard() noexcept {
	if (!impl_)
		return;
	impl_->failed = true;
	impl_->group = false;
	impl_->current.reset();
	impl_->output.reset();
	impl_->source.reset();
}
} // namespace detent::distribution
