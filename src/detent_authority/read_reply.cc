#include "read_reply.hh"
#include <rapidjson/writer.h>
#include <stdexcept>

namespace detent::authority {
namespace {
std::string identity_hex(const Identity &value) {
	constexpr char digits[] = "0123456789abcdef";
	std::string result(32, '0');
	for (std::size_t i = 0; i < value.size(); ++i) {
		auto byte = static_cast<unsigned char>(value[i]);
		result[2 * i] = digits[byte >> 4];
		result[2 * i + 1] = digits[byte & 15];
	}
	return result;
}
struct Buffer {
	using Ch = char;
	std::size_t limit, count = 0;
	bool encoding = false;
	std::string bytes;
	void Put(char byte) {
		if (count == limit)
			throw std::length_error("authority reply bound");
		++count;
		if (encoding)
			bytes += byte;
	}
	void Flush() {}
};
} // namespace
struct ReadReplyEncoder::Impl {
	enum class Phase { Header, Revisions, KnobsHeader, Definition, Overrides, DefinitionEnd, End, Finished };
	const State &state;
	std::string_view name_space;
	const std::vector<std::string> &keys;
	Context context;
	Buffer buffer;
	std::unique_ptr<rapidjson::Writer<Buffer>> writer;
	Phase phase = Phase::Header;
	std::size_t key = 0, size = 0;
	const BoolDef *boolean = nullptr;
	decltype(BoolDef::overrides)::const_iterator override;
	bool count_done = false, taken = false;
	Impl(const State &s, std::string_view ns, const std::vector<std::string> &k, Context c, std::size_t limit)
		: state(s), name_space(ns), keys(k), context(c), buffer{limit},
		  writer(std::make_unique<rapidjson::Writer<Buffer>>(buffer)) {}
	void string(std::string_view value) {
		writer->String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
	}
	void field(std::string_view value) {
		writer->Key(value.data(), static_cast<rapidjson::SizeType>(value.size()));
	}
	void step(CommandWorkBudget &budget) {
		while (phase != Phase::Finished) {
			// One token group includes at most a key and one scalar/object.
			// Charge worst-case escaping, including control bytes and NULs.
			std::size_t charge = 256;
			if (phase == Phase::Header)
				charge = 1024;
			else if (phase == Phase::Revisions || phase == Phase::Definition) {
				if (key < keys.size())
					charge += keys[key].size() * 6;
			} else if (phase == Phase::Overrides && override != boolean->overrides.end())
				charge += override->first.size() * 6;
			if (!budget.records || budget.bytes < charge)
				return;
			--budget.records;
			budget.bytes -= charge;
			switch (phase) {
			case Phase::Header:
				writer->StartObject();
				field("authority");
				writer->StartObject();
				field("cluster");
				string(identity_hex(context.cluster));
				field("group");
				string(identity_hex(context.group));
				field("epoch");
				string(identity_hex(context.epoch));
				writer->EndObject();
				field("namespace_revision");
				writer->Uint64(state.namespace_revision(name_space));
				field("key_revisions");
				writer->StartObject();
				phase = Phase::Revisions;
				break;
			case Phase::Revisions:
				if (key == keys.size()) {
					writer->EndObject();
					key = 0;
					phase = Phase::KnobsHeader;
				} else {
					const auto &name = keys[key++];
					if (auto record = state.find(name)) {
						field(name);
						writer->Uint64(record->revision);
					}
				}
				break;
			case Phase::KnobsHeader:
				field("schema_version");
				writer->Uint(1);
				field("version");
				writer->Uint64(state.revision());
				field("knobs");
				writer->StartObject();
				phase = Phase::Definition;
				break;
			case Phase::Definition:
				if (key == keys.size()) {
					phase = Phase::End;
					break;
				}
				if (auto record = state.find(keys[key++])) {
					field(keys[key - 1]);
					writer->StartObject();
					field("type");
					boolean = std::get_if<BoolDef>(record->value.get());
					if (boolean) {
						string("bool");
						field("default");
						writer->Bool(boolean->default_value);
						field("overrides");
						writer->StartObject();
						override = boolean->overrides.begin();
						phase = Phase::Overrides;
					} else {
						string("int");
						field("default");
						writer->Int64(std::get<IntDef>(*record->value).default_value);
						writer->EndObject();
					}
				}
				break;
			case Phase::Overrides:
				if (override == boolean->overrides.end()) {
					writer->EndObject();
					phase = Phase::DefinitionEnd;
				} else {
					field(override->first);
					if (auto forced = std::get_if<bool>(&override->second))
						writer->Bool(*forced);
					else {
						writer->StartObject();
						field("rollout_pct");
						writer->Uint(std::get<GroupRollout>(override->second).rollout_pct);
						writer->EndObject();
					}
					++override;
				}
				break;
			case Phase::DefinitionEnd:
				field("rollout_pct");
				if (boolean->rollout_pct)
					writer->Uint(*boolean->rollout_pct);
				else
					writer->Null();
				writer->EndObject();
				boolean = nullptr;
				phase = Phase::Definition;
				break;
			case Phase::End:
				writer->EndObject();
				writer->EndObject();
				phase = Phase::Finished;
				if (!buffer.encoding) {
					size = buffer.count;
					count_done = true;
				} else if (buffer.count != size)
					throw std::logic_error("read capture changed during encoding");
				break;
			case Phase::Finished:
				break;
			}
		}
	}
};
ReadReplyEncoder::ReadReplyEncoder(const State &state, std::string_view ns,
								   const std::vector<std::string> &keys, Context context, std::size_t limit)
	: impl_(std::make_unique<Impl>(state, ns, keys, context, limit)) {
	if (state.context() != context)
		throw std::invalid_argument("read authority context mismatch");
}
ReadReplyEncoder::~ReadReplyEncoder() = default;
ReadReplyEncoder::ReadReplyEncoder(ReadReplyEncoder &&) noexcept = default;
void ReadReplyEncoder::step(CommandWorkBudget &budget) {
	if (!impl_ || impl_->taken)
		throw std::logic_error("inactive read encoder");
	impl_->step(budget);
}
bool ReadReplyEncoder::counted() const noexcept { return impl_ && impl_->count_done; }
std::size_t ReadReplyEncoder::encoded_size() const {
	if (!counted())
		throw std::logic_error("read not counted");
	return impl_->size;
}
void ReadReplyEncoder::begin_encoding() {
	if (!counted() || impl_->buffer.encoding || impl_->taken)
		throw std::logic_error("read encoding state");
	impl_->buffer.bytes.reserve(impl_->size);
	impl_->writer = std::make_unique<rapidjson::Writer<Buffer>>(impl_->buffer);
	impl_->buffer.encoding = true;
	impl_->buffer.count = 0;
	impl_->key = 0;
	impl_->phase = Impl::Phase::Header;
}
bool ReadReplyEncoder::done() const noexcept {
	return impl_ && impl_->buffer.encoding && impl_->phase == Impl::Phase::Finished && !impl_->taken;
}
std::string ReadReplyEncoder::take() {
	if (!done())
		throw std::logic_error("read not complete");
	impl_->taken = true;
	return std::move(impl_->buffer.bytes);
}
bool ReadReplyEncoder::discard(CommandWorkBudget &) noexcept {
	impl_.reset();
	return true;
}
} // namespace detent::authority
