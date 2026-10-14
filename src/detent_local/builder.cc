#include "detent_local/builder.hh"
#include "applied_source.hh"
#include <algorithm>
#include <bit>
#include <limits>
#include <sha256.h>
#include <stdexcept>
namespace detent::local {
namespace {
void require(bool condition) {
	if (!condition)
		throw std::runtime_error("local image construction admission failed");
}
} // namespace
struct ImageBuilder::Impl {
	std::shared_ptr<const distribution::ValidatedSnapshot> source;
	std::optional<detail::AppliedImageSource> applied;
	std::shared_ptr<distribution::StorageLedger> account;
	distribution::Cursor applied_target;
	distribution::Profile applied_profile;
	GenerationId generation;
	ViewLimits limits;
	Header header;
	std::shared_ptr<distribution::OwnedBuffer> output;
	Descriptor selected;
	SHA256 hash;
	std::uint64_t ns = 0, record = 0, overrides = 0, strings = 0, string_cursor = 0, override_cursor = 0,
				  hash_offset = 0, charge = 0;
	std::uint64_t group = 0;
	bool grouping = false;
	unsigned phase = 0;
	bool failed = false;
	bool discarding = false;
	std::size_t disposal_charge = 0;
	std::uint64_t namespace_count() const {
		return applied ? applied->namespace_count() : source->namespaces().size();
	}
	std::uint64_t record_count() const {
		return applied ? applied->record_count() : source->records().size();
	}
	detail::SourceNamespace name_space(std::uint64_t ordinal) const {
		if (applied)
			return applied->name_space(ordinal);
		const auto &n = source->namespaces()[ordinal];
		return {n.name, n.revision};
	}
	detail::SourceRecord source_record(std::uint64_t ordinal) const {
		if (applied)
			return applied->record(ordinal);
		const auto &r = source->records()[ordinal];
		detail::SourceRecord result{r.namespace_index, r.key, r.revision, IntDef{}};
		if (auto b = std::get_if<distribution::WireBoolDef>(&r.definition))
			result.definition = detail::SourceBoolean{b->default_value, b->rollout_pct, b->overrides.start,
													  b->overrides.count};
		else
			result.definition = std::get<IntDef>(r.definition);
		return result;
	}
	std::optional<detail::SourceBoolean> boolean() const {
		if (record >= record_count())
			return {};
		auto r = source_record(record);
		if (auto b = std::get_if<detail::SourceBoolean>(&r.definition))
			return *b;
		return {};
	}
	detail::SourceOverride source_override(std::uint64_t ordinal) const {
		if (applied)
			return applied->override_value(record, ordinal);
		const auto &b = std::get<distribution::WireBoolDef>(source->records()[record].definition);
		const auto &r = source->overrides(b)[ordinal];
		return {r.first, r.second};
	}
	void count_strings(std::size_t n) {
		require(n <= limits.strings - strings);
		strings += n;
	}
	bool counted() {
		if (ns < namespace_count()) {
			count_strings(name_space(ns++).name.size());
			return false;
		}
		if (record < record_count()) {
			const auto &r = source_record(record);
			auto b = boolean();
			if (!grouping) {
				count_strings(r.key.size());
				if (b) {
					require(b->override_count <= limits.overrides);
					group = 0;
					grouping = true;
				}
			}
			if (b && group < b->override_count) {
				count_strings(source_override(group).name.size());
				++group;
				++overrides;
				return false;
			}
			grouping = false;
			++record;
			return false;
		}
		header = {calculate_layout(namespace_count(), record_count(), overrides, strings, limits.bytes),
				  applied ? applied_target : source->cursor(), applied ? applied_profile : source->profile()};
		output = distribution::make_owned_buffer(account, header.layout.length);
		ns = record = 0;
		phase = 1;
		return true;
	}
	void number(std::uint64_t offset, std::uint64_t value, unsigned width) {
		write_integer(output->mutable_bytes(), offset, value, width);
	}
	void text(std::uint64_t row, std::string_view value) {
		number(row, string_cursor, 8);
		number(row + 8, value.size(), 4);
		std::copy(value.begin(), value.end(),
				  output->mutable_bytes().begin() + header.layout.string_offset + string_cursor);
		string_cursor += value.size();
	}
	std::uint64_t row_cost() const {
		if (ns < namespace_count())
			return namespace_bytes + name_space(ns).name.size();
		if (record < record_count()) {
			auto b = boolean();
			if (grouping && b && group < b->override_count)
				return override_bytes + source_override(group).name.size();
			return record_bytes + source_record(record).key.size();
		}
		return 0;
	}
	void row() {
		const auto &l = header.layout;
		if (ns < namespace_count()) {
			auto r = l.namespace_offset + ns * namespace_bytes;
			const auto value = name_space(ns++);
			text(r, value.name);
			number(r + 16, value.revision, 8);
			return;
		}
		if (record < record_count()) {
			auto b = boolean();
			if (grouping && b) {
				if (group < b->override_count) {
					auto r = l.override_offset + override_cursor++ * override_bytes;
					const auto [name, value] = source_override(group);
					text(r, name);
					if (auto forced = std::get_if<bool>(&value))
						number(r + 13, *forced, 1);
					else {
						number(r + 12, 1, 1);
						number(r + 13, std::get<GroupRollout>(value).rollout_pct, 1);
					}
					++group;
				}
				if (group == b->override_count) {
					grouping = false;
					++record;
				}
				return;
			}
			const auto &value = source_record(record);
			auto r = l.record_offset + record * record_bytes;
			text(r, value.key);
			number(r + 12, value.namespace_index, 4);
			number(r + 16, value.revision, 8);
			number(r + 32, override_cursor, 8);
			if (b) {
				number(r + 24, b->default_value, 8);
				number(r + 40, b->override_count, 4);
				number(r + 45, b->rollout_pct.has_value(), 1);
				number(r + 46, b->rollout_pct.value_or(0), 1);
				if (b->override_count) {
					group = 0;
					grouping = true;
				} else
					++record;
			} else {
				number(r + 24, std::bit_cast<std::uint64_t>(std::get<IntDef>(value.definition).default_value),
					   8);
				number(r + 44, 1, 1);
				++record;
			}
			return;
		}
		require(string_cursor == l.strings && override_cursor == l.overrides);
		phase = 3;
	}
};
ImageBuilder::ImageBuilder(std::shared_ptr<const distribution::ValidatedSnapshot> source,
						   GenerationId generation, ViewLimits limits)
	: impl_([&] {
		  require(static_cast<bool>(source));
		  return distribution::make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free);
	  }()) {
	require(source && source->namespaces().size() <= limits.namespaces &&
			source->records().size() <= limits.records && generation != GenerationId{});
	impl_->account = source->storage_account();
	impl_->source = std::move(source);
	impl_->generation = generation;
	impl_->limits = limits;
}
ImageBuilder::ImageBuilder(std::shared_ptr<const distribution::ValidatedDelta> source,
						   RecoveredGeneration base, GenerationId generation, ViewLimits limits)
	: impl_([&] {
		  require(static_cast<bool>(source));
		  return distribution::make_storage_owner<Impl>(source->storage_account(), std::malloc, std::free);
	  }()) {
	require(generation != GenerationId{});
	impl_->account = source->storage_account();
	impl_->applied_target = source->target();
	impl_->applied_profile = source->profile();
	impl_->applied.emplace(std::move(base), std::move(source), limits);
	impl_->generation = generation;
	impl_->limits = limits;
}
ImageBuilder::~ImageBuilder() = default;
ImageBuilder::ImageBuilder(ImageBuilder &&) noexcept = default;
void ImageBuilder::step(ValidationBudget &budget) {
	if (!impl_ || impl_->failed)
		throw std::logic_error("inactive local image builder");
	auto &p = *impl_;
	try {
		if (p.applied && !p.applied->done()) {
			p.applied->step(budget);
			if (!p.applied->done())
				return;
			require(p.namespace_count() <= p.limits.namespaces && p.record_count() <= p.limits.records);
		}
		while (p.phase < 5) {
			if ((p.phase == 0 || p.phase == 2) && p.applied && p.ns >= p.namespace_count() &&
				p.record < p.record_count() && !p.applied->prepare_record(p.record, budget))
				return;
			if (p.phase == 0) {
				if (!budget.rows)
					return;
				--budget.rows;
				p.counted();
			} else if (p.phase == 1) {
				auto remaining = p.header.layout.length - p.output->size();
				auto count = std::min<std::uint64_t>(remaining, budget.bytes);
				if (!count && remaining)
					return;
				p.output->resize(p.output->size() + count);
				budget.bytes -= count;
				if (p.output->size() == p.header.layout.length) {
					auto h = encode_header(p.header);
					std::copy(h.begin(), h.end(), p.output->mutable_bytes().begin());
					p.phase = 2;
				}
			} else if (p.phase == 2) {
				if (!budget.rows)
					return;
				if (!p.charge)
					p.charge = p.row_cost();
				auto n = std::min<std::uint64_t>(p.charge, budget.bytes);
				budget.bytes -= n;
				p.charge -= n;
				if (p.charge)
					return;
				--budget.rows;
				p.row();
			} else if (p.phase == 3) {
				auto remaining = p.output->size() - p.hash_offset;
				auto n = std::min<std::uint64_t>(remaining, budget.bytes);
				if (!n && remaining)
					return;
				p.hash.add(p.output->data() + p.hash_offset, n);
				p.hash_offset += n;
				budget.bytes -= n;
				if (p.hash_offset == p.output->size()) {
					unsigned char digest[32];
					p.hash.getHash(digest);
					p.selected = {p.generation, p.output->size(), {}, p.header.cursor, p.header.profile};
					std::copy_n(reinterpret_cast<char *>(digest), 32, p.selected.checksum.begin());
					p.output->seal();
					p.phase = 4;
				}
			} else {
				if (p.applied && !p.applied->discard(budget))
					return;
				p.phase = 5;
			}
		}
	} catch (...) {
		p.failed = true;
		throw;
	}
}
bool ImageBuilder::done() const noexcept { return impl_ && impl_->phase == 5 && !impl_->failed; }
std::shared_ptr<const distribution::OwnedBuffer> ImageBuilder::bytes() const {
	if (!done())
		throw std::logic_error("local image not sealed");
	return impl_->output;
}
Descriptor ImageBuilder::descriptor() const {
	if (!done())
		throw std::logic_error("local image not sealed");
	return impl_->selected;
}
std::optional<Descriptor> ImageBuilder::base_descriptor() const {
	if (!impl_ || !impl_->applied)
		return {};
	return impl_->applied->base_descriptor();
}
std::optional<DirectoryIdentity> ImageBuilder::base_directory_identity() const {
	if (!impl_ || !impl_->applied)
		return {};
	return impl_->applied->directory_identity();
}
bool ImageBuilder::discard(ValidationBudget &budget) {
	if (!impl_)
		return true;
	auto &p = *impl_;
	p.failed = true;
	if (!p.discarding) {
		p.discarding = true;
		p.disposal_charge = p.output ? p.output->storage_capacity() : 0;
	}
	if (p.applied && !p.applied->discard(budget))
		return false;
	if (p.output) {
		if (!budget.rows)
			return false;
		const auto n = std::min(p.disposal_charge, budget.bytes);
		p.disposal_charge -= n;
		budget.bytes -= n;
		if (p.disposal_charge)
			return false;
		--budget.rows;
		p.output.reset();
	}
	return true;
}
} // namespace detent::local
