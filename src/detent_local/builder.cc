#include "detent_local/builder.hh"
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
	GenerationId generation;
	ViewLimits limits;
	Header header;
	std::shared_ptr<std::string> output = std::make_shared<std::string>();
	Descriptor selected;
	SHA256 hash;
	std::uint64_t ns = 0, record = 0, overrides = 0, strings = 0, string_cursor = 0, override_cursor = 0,
				  hash_offset = 0, charge = 0;
	using Iterator = distribution::OverrideRange::const_iterator;
	std::optional<Iterator> group;
	unsigned phase = 0;
	bool failed = false;
	const distribution::WireBoolDef *boolean() const {
		return record < source->records().size()
				   ? std::get_if<distribution::WireBoolDef>(&source->records()[record].definition)
				   : nullptr;
	}
	void count_strings(std::size_t n) {
		require(n <= limits.strings - strings);
		strings += n;
	}
	bool counted() {
		if (ns < source->namespaces().size()) {
			count_strings(source->namespaces()[ns++].name.size());
			return false;
		}
		if (record < source->records().size()) {
			const auto &r = source->records()[record];
			auto b = boolean();
			if (!group) {
				count_strings(r.key.size());
				if (b) {
					require(b->overrides.count <= limits.overrides);
					group = source->overrides(*b).begin();
				}
			}
			if (b && *group != source->overrides(*b).end()) {
				count_strings((**group).first.size());
				++*group;
				++overrides;
				return false;
			}
			group.reset();
			++record;
			return false;
		}
		header = {calculate_layout(source->namespaces().size(), source->records().size(), overrides, strings,
								   limits.bytes),
				  source->cursor(), source->profile()};
		output->reserve(header.layout.length);
		ns = record = 0;
		phase = 1;
		return true;
	}
	void number(std::uint64_t offset, std::uint64_t value, unsigned width) {
		write_integer(std::span<char>(*output), offset, value, width);
	}
	void text(std::uint64_t row, std::string_view value) {
		number(row, string_cursor, 8);
		number(row + 8, value.size(), 4);
		std::copy(value.begin(), value.end(), output->begin() + header.layout.string_offset + string_cursor);
		string_cursor += value.size();
	}
	std::uint64_t row_cost() const {
		if (ns < source->namespaces().size())
			return namespace_bytes + source->namespaces()[ns].name.size();
		if (record < source->records().size()) {
			auto b = boolean();
			if (group && b && *group != source->overrides(*b).end())
				return override_bytes + (**group).first.size();
			return record_bytes + source->records()[record].key.size();
		}
		return 0;
	}
	void row() {
		const auto &l = header.layout;
		if (ns < source->namespaces().size()) {
			auto r = l.namespace_offset + ns * namespace_bytes;
			const auto &value = source->namespaces()[ns++];
			text(r, value.name);
			number(r + 16, value.revision, 8);
			return;
		}
		if (record < source->records().size()) {
			auto b = boolean();
			if (group && b) {
				if (*group != source->overrides(*b).end()) {
					auto r = l.override_offset + override_cursor++ * override_bytes;
					const auto &[name, value] = **group;
					text(r, name);
					if (auto forced = std::get_if<bool>(&value))
						number(r + 13, *forced, 1);
					else {
						number(r + 12, 1, 1);
						number(r + 13, std::get<GroupRollout>(value).rollout_pct, 1);
					}
					++*group;
				}
				if (*group == source->overrides(*b).end()) {
					group.reset();
					++record;
				}
				return;
			}
			const auto &value = source->records()[record];
			auto r = l.record_offset + record * record_bytes;
			text(r, value.key);
			number(r + 12, value.namespace_index, 4);
			number(r + 16, value.revision, 8);
			number(r + 32, override_cursor, 8);
			if (b) {
				number(r + 24, b->default_value, 8);
				number(r + 40, b->overrides.count, 4);
				number(r + 45, b->rollout_pct.has_value(), 1);
				number(r + 46, b->rollout_pct.value_or(0), 1);
				if (b->overrides.count)
					group = source->overrides(*b).begin();
				else
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
	: impl_(std::make_unique<Impl>()) {
	require(source && source->namespaces().size() <= limits.namespaces &&
			source->records().size() <= limits.records && generation != GenerationId{});
	impl_->source = std::move(source);
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
		while (p.phase < 4) {
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
					std::copy(h.begin(), h.end(), p.output->begin());
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
			} else {
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
					p.phase = 4;
				}
			}
		}
	} catch (...) {
		p.failed = true;
		throw;
	}
}
bool ImageBuilder::done() const noexcept { return impl_ && impl_->phase == 4 && !impl_->failed; }
std::shared_ptr<const std::string> ImageBuilder::bytes() const {
	if (!done())
		throw std::logic_error("local image not sealed");
	return impl_->output;
}
Descriptor ImageBuilder::descriptor() const {
	if (!done())
		throw std::logic_error("local image not sealed");
	return impl_->selected;
}
} // namespace detent::local
