#include "export_encoder.hh"
#include "detent_distribution/messagepack.hh"
#include <algorithm>
namespace detent::authority {
namespace d = distribution;
namespace m = distribution::messagepack;
struct SnapshotExportEncoder::Impl {
	static constexpr std::size_t chunk_bytes = 65536;
	std::shared_ptr<ExportCapture> capture;
	d::Limits limits;
	std::shared_ptr<d::OwnedBuffer> output;
	m::InlineBytes<512> pending;
	std::optional<ExportCapture::RecordCursor> cursor;
	std::optional<ExportCapture::RecordCursor::Row> current;
	std::optional<decltype(BoolDef::overrides)::const_iterator> group;
	std::uint64_t namespaces = 0, records = 0, overrides = 0, strings = 0, emitted = 0, wire_length = 0;
	std::size_t offset = 0, charge = 0, definition_bytes = 0;
	unsigned phase = 0;
	bool ready = false, finished = false, failed = false;
	Impl(std::shared_ptr<ExportCapture> source, d::Limits bounds)
		: capture(std::move(source)), limits(std::move(bounds)),
		  output(d::make_owned_buffer(capture->storage_account(), chunk_bytes)) {
		if (capture->scope().size() > limits.namespaces)
			throw std::length_error("export scope limit");
	}
	void count_text(std::string_view value) {
		if (value.size() > limits.string_bytes - strings)
			throw std::length_error("export string limit");
		strings += value.size();
	}
	void advance_record() {
		if (current)
			return;
		if (!cursor) {
			if (namespaces == capture->scope().size())
				return;
			cursor.emplace(capture->records(namespaces++));
			return;
		}
		current = cursor->next();
		if (!current)
			cursor.reset();
	}
	void count_wire(const m::InlineBytes<512> &bytes) {
		if (bytes.size() > limits.bytes - wire_length)
			throw std::length_error("export wire byte limit");
		wire_length += bytes.size();
	}
	void namespace_row(m::InlineBytes<512> &out, std::uint64_t ordinal) const {
		m::array(out, 2);
		m::string(out, capture->scope().name(ordinal));
		m::unsigned_integer(out, capture->namespace_revision(ordinal));
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
	static void record_row(m::InlineBytes<512> &out, const ExportCapture::RecordCursor::Row &row) {
		m::array(out, 4);
		m::unsigned_integer(out, row.namespace_index);
		m::string(out, row.key);
		m::unsigned_integer(out, row.revision);
		if (auto flag = std::get_if<BoolDef>(row.definition)) {
			m::array(out, 4);
			m::unsigned_integer(out, 0);
			out.push_back(flag->default_value ? '\xc3' : '\xc2');
			if (flag->rollout_pct)
				m::unsigned_integer(out, *flag->rollout_pct);
			else
				out.push_back('\xc0');
			m::array(out, flag->overrides.size());
		} else {
			m::array(out, 2);
			m::unsigned_integer(out, 1);
			m::signed_integer(out, std::get<IntDef>(*row.definition).default_value);
		}
	}
	void count_one() {
		m::InlineBytes<512> encoded;
		if (phase == 0) {
			if (namespaces < capture->scope().size()) {
				count_text(capture->scope().name(namespaces));
				namespace_row(encoded, namespaces++);
				count_wire(encoded);
			} else {
				namespaces = 0;
				phase = 1;
			}
			return;
		}
		if (group) {
			const auto &flag = std::get<BoolDef>(*current->definition);
			if (*group != flag.overrides.end()) {
				const auto &entry = **group;
				definition_bytes +=
					9 + entry.first.size() + (std::holds_alternative<bool>(entry.second) ? 1 : 8);
				if (definition_bytes > limits.definition_bytes)
					throw std::length_error("export definition limit");
				count_text(entry.first);
				override_row(encoded, entry.first, entry.second);
				count_wire(encoded);
				++*group;
				++overrides;
			} else {
				group.reset();
				current.reset();
			}
			return;
		}
		if (!current) {
			if (!cursor && namespaces == capture->scope().size()) {
				header(encoded);
				m::array(encoded, records);
				count_wire(encoded);
				namespaces = 0;
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
		if (const auto *flag = std::get_if<BoolDef>(current->definition)) {
			if (flag->overrides.size() > limits.overrides_per_definition)
				throw std::length_error("export override limit");
			definition_bytes = flag->rollout_pct ? 19 : 11;
			if (definition_bytes > limits.definition_bytes)
				throw std::length_error("export definition limit");
			group = flag->overrides.begin();
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
		const auto &id = capture->cursor();
		m::array(out, 5);
		for (auto *field : {&id.cluster, &id.group, &id.restore_epoch})
			m::string(out, {field->data(), field->size()}, true);
		m::string(out, {id.scope.data(), id.scope.size()}, true);
		m::unsigned_integer(out, id.revision);
		m::array(out, 4);
		m::unsigned_integer(out, capture->scope().size());
		m::unsigned_integer(out, records);
		m::unsigned_integer(out, overrides);
		m::unsigned_integer(out, strings);
		m::array(out, capture->scope().size());
	}
	void emit_one() {
		if (phase == 2) {
			header(pending);
			phase = 3;
			return;
		}
		if (phase == 3) {
			if (namespaces < capture->scope().size())
				namespace_row(pending, namespaces++);
			else {
				m::array(pending, records);
				namespaces = 0;
				phase = 4;
			}
			return;
		}
		if (group) {
			const auto &flag = std::get<BoolDef>(*current->definition);
			if (*group != flag.overrides.end()) {
				const auto &entry = **group;
				override_row(pending, entry.first, entry.second);
				++*group;
			} else {
				group.reset();
				current.reset();
			}
			return;
		}
		if (!current) {
			if (!cursor && namespaces == capture->scope().size()) {
				if (emitted != wire_length)
					throw std::logic_error("export count/emission mismatch");
				finished = true;
				return;
			}
			advance_record();
			return;
		}
		record_row(pending, *current);
		if (auto flag = std::get_if<BoolDef>(current->definition))
			group = flag->overrides.begin();
		else
			current.reset();
	}
};
SnapshotExportEncoder::SnapshotExportEncoder(std::shared_ptr<ExportCapture> capture, d::Limits limits)
	: impl_([&] {
		  if (!capture || (limits.storage_account && limits.storage_account != capture->storage_account()))
			  throw std::invalid_argument("export capture account mismatch");
		  return d::make_storage_owner<Impl>(capture->storage_account(), std::malloc, std::free, capture,
											 limits);
	  }()) {}
SnapshotExportEncoder::~SnapshotExportEncoder() = default;
SnapshotExportEncoder::SnapshotExportEncoder(SnapshotExportEncoder &&) noexcept = default;
void SnapshotExportEncoder::step(d::WorkBudget &budget) {
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
			p.output = d::make_owned_buffer(p.capture->storage_account(), Impl::chunk_bytes);
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
				p.output = d::make_owned_buffer(p.capture->storage_account(), Impl::chunk_bytes);
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
std::uint64_t SnapshotExportEncoder::encoded_length() const {
	if (!impl_ || impl_->failed || impl_->phase < 2)
		throw std::logic_error("snapshot export count incomplete");
	return impl_->wire_length;
}
bool SnapshotExportEncoder::counted() const noexcept { return impl_ && !impl_->failed && impl_->phase >= 2; }
bool SnapshotExportEncoder::done() const noexcept {
	return impl_ && impl_->finished && !impl_->ready && !impl_->failed;
}
bool SnapshotExportEncoder::chunk_ready() const noexcept { return impl_ && impl_->ready && !impl_->failed; }
std::shared_ptr<const d::OwnedBuffer> SnapshotExportEncoder::take_chunk() {
	if (!chunk_ready())
		throw std::logic_error("export chunk unavailable");
	impl_->ready = false;
	return std::exchange(impl_->output, {});
}
void SnapshotExportEncoder::discard() noexcept {
	if (!impl_)
		return;
	impl_->failed = true;
	impl_->group.reset();
	impl_->current.reset();
	impl_->cursor.reset();
	impl_->output.reset();
	impl_->capture.reset();
}
} // namespace detent::authority
