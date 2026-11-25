// The checkpoint image: snapshots, the image encoder and decoder, and State::restore.
#include "state_internal.hh"
#include "detent/utf8.hh"
#include "state.hh"
#include "deferred_values.hh"
#include "history_format.hh"
#include "detent/validation.hh"
#include "prepared.hh"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <sha256.h>
#include <stdexcept>

namespace detent::authority {
Snapshot State::snapshot(std::string_view name_space) const {
	Snapshot s;
	s.version = revision_;
	s.source_id = "authority";
	records_.for_each([&](const std::string &key, const Record &r) {
		if (name_space.empty() || key.substr(0, key.find(':')) == name_space) {
			s.knobs.emplace(key, *r.value);
		}
	});
	return s;
}
ImageEncoder::ImageEncoder(State state)
	: state_(std::move(state)), records_(state_.records_.cursor()), namespaces_(state_.namespaces_.cursor()),
	  producers_(state_.producers_.cursor()), touched_(state_.touched_.cursor()) {}
void ImageEncoder::row() {
	Writer w{{}, state_.limits_.image_bytes};
	for (;;) {
		if (stage_ == 0) {
			w.raw("DTAIMG03");
			encode_context(w, state_.context_);
			write_limits(w, state_.limits_);
			w.number(state_.applied_);
			w.number(state_.applied_term_);
			w.number(state_.revision_);
			w.number(state_.records_.size());
			w.identity(state_.seed_);
			w.number(state_.directory_head_);
			write_policy(w, state_.policy_);
			w.flag(state_.pending_.has_value());
			if (state_.pending_)
				write_policy(w, *state_.pending_);
			const auto &c = state_.seal_;
			for (auto n : {c.ordinal, c.index, c.transactions, c.bytes, c.entries, c.log_bytes})
				w.number(n);
			w.flag(c.forced);
			++stage_;
		} else if (stage_ == 1) {
			if (auto item = records_.next()) {
				w.text(*item->first);
				w.number(item->second->revision);
				definition(w, *item->second->value, state_.limits_);
			} else {
				++stage_;
				continue;
			}
		} else if (stage_ == 2) {
			w.number(state_.namespaces_.size());
			++stage_;
		} else if (stage_ == 3) {
			if (auto item = namespaces_.next()) {
				// Live sizes are recomputed from the records.
				w.text(*item->first);
				w.number(item->second->position);
				w.number(item->second->directory);
				w.number(item->second->origin);
				w.flag(item->second->live);
				w.identity(item->second->incarnation);
			} else {
				++stage_;
				continue;
			}
		} else if (stage_ == 4) {
			w.number(state_.producers_.size());
			++stage_;
		} else if (stage_ == 5) {
			if (auto item = producers_.next()) {
				auto &p = *item->second;
				w.text(*item->first);
				w.number(p->generation);
				w.number(p->high_water);
				w.flag(p->active);
				w.identity(p->nonce);
				w.number(p->receipts.size());
				for (const auto &receipt : p->receipts) {
					w.number(receipt.sequence);
					w.raw({reinterpret_cast<const char *>(receipt.digest.data()), receipt.digest.size()});
					write_result(w, receipt.result);
				}
			} else {
				++stage_;
				continue;
			}
		} else if (stage_ == 6) {
			w.number(state_.touched_.size());
			++stage_;
		} else if (stage_ == 7) {
			if (auto item = touched_.next()) {
				w.text(*item->first);
				w.number(*item->second);
			} else {
				stage_ = 9;
				continue;
			}
		} else {
			return;
		}
		pending_ = std::move(w.bytes);
		offset_ = 0;
		return;
	}
}
std::string ImageEncoder::next() {
	std::string output;
	output.reserve(65536);
	while (output.size() < 65536) {
		if (pending_.empty()) {
			row();
			if (finished()) {
				break;
			}
		}
		auto count = std::min<std::size_t>(65536 - output.size(), pending_.size() - offset_);
		require(count <= state_.limits_.image_bytes - written_, "application image bound");
		output.append(pending_, offset_, count);
		offset_ += count;
		written_ += count;
		if (offset_ == pending_.size()) {
			pending_.clear();
			offset_ = 0;
		}
	}
	return output;
}
std::string State::checkpoint() const {
	ImageEncoder encoder(*this);
	std::string image;
	while (!encoder.finished()) {
		image += encoder.next();
	}
	return image;
}

struct ImageDecoder::Impl {
	State state;
	std::string buffer, previous;
	std::size_t received = 0;
	std::uint64_t remaining = 0, position = 0, namespace_bytes = 0, counted_bytes = 0;
	std::string counting; // The namespace whose live size stage 4 is summing.
	unsigned stage = 0;
	bool final = false, waiting = true, complete = false, released = false;
	std::optional<decltype(State::records_)::Cursor> records;
	Impl(Context context, Limits limits) : state(context, limits) {}
	// A namespace's records are contiguous in key order, so its live size is one run.
	void count_namespace() {
		if (!counting.empty()) {
			auto entry = *state.namespaces_.find(counting);
			entry.bytes = namespace_bytes;
			state.namespaces_ = state.namespaces_.set(counting, entry);
		}
		counting.clear();
		namespace_bytes = 0;
	}
	void row(Reader &r) {
		auto &s = state;
		auto &l = s.limits_;
		if (stage == 0) {
			require(r.raw(8) == "DTAIMG03" && decode_context(r) == s.context_, "foreign application image");
			(void)read_limits(r, l); // The state runs under the configured limits.
			auto applied = r.number(), term = r.number(), revision = r.number(), keys = r.number();
			require(applied < maximum && bool(applied) == bool(term), "invalid image boundary");
			require(keys <= l.keys, "image key count bound");
			s.seed_ = r.identity();
			s.directory_head_ = r.number();
			require(s.positions_enabled() || !s.directory_head_, "invalid namespace directory");
			s.policy_ = read_policy(r);
			if (r.flag())
				s.pending_ = read_policy(r);
			auto &c = s.seal_;
			for (auto *n : {&c.ordinal, &c.index, &c.transactions, &c.bytes, &c.entries, &c.log_bytes})
				*n = r.number();
			c.forced = r.flag();
			require(c.index <= applied && (s.positions_enabled() || c == SealCounters{}) &&
						(c.index || !c.ordinal),
					"invalid seal counters");
			s.applied_ = applied;
			s.applied_term_ = term;
			s.revision_ = revision;
			remaining = keys;
			position = 0;
			stage = 1;
		} else if (stage == 1) {
			if (!remaining) {
				stage = 2;
				previous.clear();
				return;
			}
			auto key = r.text(200);
			auto revision = r.number();
			require(is_valid_knob_name(key) && (!position || previous < key) && revision &&
						revision <= s.revision_,
					"invalid image key");
			auto definition_start = r.bytes.size();
			auto value = definition(r, l);
			auto definition_bytes = definition_start - r.bytes.size();
			auto amount = key.size() + definition_bytes + 16;
			require(amount <= l.state_bytes - s.bytes_, "logical state bound");
			s.records_ =
				s.records_.set(key, {revision, move_definition(*s.definitions_, value, definition_bytes)});
			s.bytes_ += amount;
			previous = std::move(key);
			--remaining;
			++position;
		} else if (stage == 2) {
			auto count = r.number();
			require(count <= l.namespaces, "namespace count bound");
			remaining = count;
			position = 0;
			stage = 3;
		} else if (stage == 3) {
			if (!remaining) {
				records.emplace(s.records_.cursor());
				stage = 4;
				previous.clear();
				return;
			}
			auto name = r.text(199);
			Namespace entry;
			entry.position = r.number();
			entry.directory = r.number();
			entry.origin = r.number();
			entry.live = r.flag();
			entry.incarnation = r.identity();
			require(is_valid_knob_name(name + ":x") && (!position || previous < name) && entry.position &&
						entry.position <= s.revision_ && entry.origin <= entry.position,
					"invalid namespace record");
			if (s.positions_enabled()) {
				require(entry.directory && entry.directory <= s.directory_head_ &&
							!s.directory_.find(entry.directory),
						"invalid namespace directory");
				// A live namespace's incarnation is its creation's; a tombstone keeps the dropped one.
				require(entry.live ? entry.incarnation ==
										 derive_incarnation(s.context_, s.seed_, entry.directory, name)
								   : entry.incarnation != Identity{},
						"invalid namespace incarnation");
				s.directory_ = s.directory_.set(entry.directory, name);
			} else
				require(!entry.directory && entry.live && entry.incarnation == Identity{},
						"invalid namespace directory");
			s.namespaces_ = s.namespaces_.set(name, entry);
			previous = std::move(name);
			--remaining;
			++position;
		} else if (stage == 4) {
			if (auto item = records->next()) {
				auto name = std::string_view(*item->first).substr(0, item->first->find(':'));
				auto entry = s.namespaces_.find(name);
				require(entry && entry->live && entry->position >= item->second->revision,
						"missing namespace revision");
				if (name != counting) {
					count_namespace();
					counting = name;
				}
				auto amount = stored_footprint(*item->first, *item->second);
				namespace_bytes += amount;
				counted_bytes += amount;
			} else {
				count_namespace();
				require(counted_bytes == s.bytes_, "namespace size mismatch");
				records.reset();
				stage = 5;
			}
		} else if (stage == 5) {
			auto count = r.number();
			require(count <= l.producers, "producer count bound");
			remaining = count;
			position = 0;
			stage = 6;
		} else if (stage == 6) {
			if (!remaining) {
				stage = 7;
				return;
			}
			auto name = r.text(128);
			require(!name.empty() && valid_utf8(name) && (!position || previous < name),
					"invalid producer record");
			auto p = s.producer_values_->create(41);
			p->generation = r.number();
			p->high_water = r.number();
			p->active = r.flag();
			p->nonce = r.identity();
			auto receipts = r.number();
			require(p->generation && p->nonce != Identity{} &&
						receipts == std::min<std::uint64_t>(p->high_water, l.results),
					"invalid receipt window");
			detail::DeferredValues<Producer>::set_encoded_size(*p, 41 + receipts * 80);
			for (std::uint64_t j = 0; j < receipts; ++j) {
				Receipt receipt;
				receipt.sequence = r.number();
				auto dg = r.raw(32);
				std::copy(dg.begin(), dg.end(), receipt.digest.begin());
				receipt.result = read_result(r);
				require(receipt.sequence == p->high_water - receipts + j + 1 &&
							receipt.result.sequence == receipt.sequence &&
							receipt.result.generation == p->generation &&
							receipt.result.revision <= s.revision_ &&
							receipt.result.namespace_revision <= s.revision_ &&
							(receipt.result.status == Status::Applied ||
							 receipt.result.status == Status::NoChange ||
							 receipt.result.status == Status::Conflict ||
							 receipt.result.status == Status::Capacity ||
							 receipt.result.status == Status::Exhausted),
						"invalid receipt correlation");
				p->receipts.push_back(receipt);
			}
			s.producers_ = s.producers_.set(name, p);
			previous = std::move(name);
			--remaining;
			++position;
		} else if (stage == 7) {
			auto count = r.number();
			require(count <= l.keys + s.seal_.transactions * l.mutations, "accumulator count bound");
			require(s.positions_enabled() || !count, "accumulator before positions");
			remaining = count;
			position = 0;
			previous.clear();
			stage = 8;
		} else if (stage == 8) {
			if (!remaining) {
				stage = 9;
				return;
			}
			auto key = r.text(200);
			auto at = r.number();
			auto name = std::string_view(key).substr(0, key.find(':'));
			auto entry = s.namespaces_.find(name);
			require(is_valid_knob_name(key) && (!position || previous < key) && entry && entry->live &&
						at > entry->origin && at <= entry->position,
					"invalid accumulator key");
			s.touched_ = s.touched_.set(key, at);
			previous = std::move(key);
			--remaining;
			++position;
		} else {
			require(r.bytes.empty(), "trailing authority bytes");
			if (!final)
				throw Incomplete{};
			stage = 10;
		}
	}
};
ImageDecoder::ImageDecoder(Context context, Limits limits) : impl_(std::make_unique<Impl>(context, limits)) {}
ImageDecoder::~ImageDecoder() = default;
ImageDecoder::ImageDecoder(ImageDecoder &&) noexcept = default;
ImageDecoder &ImageDecoder::operator=(ImageDecoder &&) noexcept = default;
void ImageDecoder::feed(std::string_view bytes, bool final) {
	auto &d = *impl_;
	require(!d.released && d.waiting && !d.final && d.stage != 10, "image decoder input state");
	require(bytes.size() <= 65536 && bytes.size() <= d.state.limits_.image_bytes - d.received,
			"application image bound");
	// A producer row is the largest variable item; no whole-image buffering.
	auto row_bound =
		std::max<std::size_t>(d.state.limits_.definition_bytes + 256, 177 + 80 * d.state.limits_.results);
	require(bytes.size() <= row_bound + 65536 - d.buffer.size(), "image decoder row bound");
	d.buffer.append(bytes);
	d.received += bytes.size();
	d.final = final;
	d.waiting = false;
}
void ImageDecoder::step(std::size_t rows) {
	auto &d = *impl_;
	if (d.complete)
		return;
	require(!d.released, "released image decoder");
	if (d.state.reclamation_pending()) {
		CommandWorkBudget cleanup;
		d.state.collect(cleanup);
		if (d.state.reclamation_pending())
			return;
	}
	if (d.stage == 10) {
		d.complete = true;
		return;
	}
	if (d.waiting)
		return;
	std::size_t consumed = 0;
	auto allocations = d.state.allocations();
	try {
		for (std::size_t i = 0; i < rows && d.stage != 10; ++i) {
			Reader r{std::string_view(d.buffer).substr(consumed)};
			auto before = r.bytes.size();
			try {
				d.row(r);
			} catch (const Incomplete &) {
				if (d.final)
					throw;
				d.waiting = true;
				break;
			}
			consumed += before - r.bytes.size();
		}
	} catch (...) {
		d.buffer.erase(0, consumed);
		throw;
	}
	d.buffer.erase(0, consumed);
	d.state.collect(d.state.allocations() - allocations + 128);
	d.complete = d.stage == 10 && !d.state.reclamation_pending();
}
bool ImageDecoder::needs_input() const noexcept {
	return !impl_->released && !impl_->complete && impl_->waiting && !impl_->state.reclamation_pending();
}
bool ImageDecoder::finished() const noexcept { return impl_->complete; }
State ImageDecoder::take() {
	require(!impl_->released && finished(), "incomplete application image");
	impl_->released = true;
	return std::move(impl_->state);
}
State ImageDecoder::discard() {
	require(!impl_->released, "released image decoder");
	impl_->released = true;
	impl_->records.reset();
	impl_->state.retire();
	return std::move(impl_->state);
}
State State::restore(std::string_view bytes, Context ctx, Limits limits) {
	ImageDecoder decoder(ctx, limits);
	std::size_t offset = 0;
	while (!decoder.finished()) {
		if (decoder.needs_input()) {
			auto count = std::min<std::size_t>(65536, bytes.size() - offset);
			decoder.feed(bytes.substr(offset, count), offset + count == bytes.size());
			offset += count;
		}
		decoder.step();
	}
	return decoder.take();
}
} // namespace detent::authority
