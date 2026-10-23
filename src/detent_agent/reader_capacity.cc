#include "reader_capacity.hh"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace detent_agent {
bool is_reader_option(std::string_view flag) {
	if (!flag.starts_with("--reader-")) return false;
	const auto suffix = flag.substr(9);
	return std::any_of(std::begin(reader_fields), std::end(reader_fields), [&](auto field) {
		std::string option(field);
		std::replace(option.begin(), option.end(), '_', '-');
		return suffix == option;
	});
}
ReaderCapacity::ReaderCapacity(const Setting &setting) {
	std::array<std::optional<std::uint64_t>, std::size(reader_fields)> values;
	bool configured = false;
	for (std::size_t i = 0; i != values.size(); ++i) {
		std::string suffix(reader_fields[i]), env = "DETENT_READER_" + suffix;
		std::transform(env.begin(), env.end(), env.begin(), [](unsigned char c) { return std::toupper(c); });
		std::replace(suffix.begin(), suffix.end(), '_', '-');
		if (auto text = setting("--reader-" + suffix, env.c_str())) {
			std::uint64_t value = 0;
			const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
			if (error != std::errc{} || end != text->data() + text->size() || (!value && i != 2))
				throw std::invalid_argument("invalid reader capacity: " + suffix);
			values[i] = value;
			configured = true;
		}
	}
	if (!configured) return;
	auto narrow = [&](std::size_t i, std::uint64_t maximum, auto &target) {
		if (!values[i]) return;
		if (*values[i] > maximum) throw std::invalid_argument("reader capacity exceeds format width");
		target = static_cast<std::remove_reference_t<decltype(target)>>(*values[i]);
	};
	narrow(0, UINT32_MAX, limits_.records);
	narrow(1, UINT32_MAX, limits_.namespaces);
	narrow(2, 256, limits_.overrides);
	narrow(3, INT64_MAX, limits_.bytes);
	narrow(4, INT64_MAX, limits_.strings);
	narrow(5, UINT64_MAX, limits_.pages);
	narrow(6, UINT64_MAX, limits_.segments);
	const auto heap = values[7].value_or(8 * 1024 * 1024);
	const auto mappings = values[8].value_or(512 * 1024 * 1024);
	const auto owners = values[9].value_or(16384);
	if (heap > SIZE_MAX || mappings > SIZE_MAX || owners > SIZE_MAX)
		throw std::invalid_argument("reader resource capacity exceeds host width");
	baseline_ = std::make_shared<detent::distribution::StorageLedger>(1024 * 1024);
	heap_ = std::make_shared<detent::distribution::StorageLedger>(heap);
	runtime_ = std::make_unique<detent::distribution::RetirementRuntime>(baseline_, 1);
	domain_.emplace(runtime_->domain(heap_, owners));
	limits_.retirement_domain = &*domain_;
	limits_.mapping_account = std::make_shared<detent::distribution::StorageLedger>(mappings);
}
} // namespace detent_agent
