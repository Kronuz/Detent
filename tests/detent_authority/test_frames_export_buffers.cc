// A voter's session answer holds each compressed fragment at its own size: a directory answer of
// several fragments, compressed, admits the bytes it sends; raw, its full fragments keep their buffers.
#include "frames_export.hh"
#include <cstdio>
#include <iostream>
#include <source_location>

namespace a = detent::authority;
namespace d = detent::distribution;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value)
		throw std::runtime_error("frames export buffer invariant at line " + std::to_string(where.line()));
}
std::vector<std::shared_ptr<const d::OwnedBuffer>> answer(bool compress, std::shared_ptr<d::StorageLedger> account,
														   std::uint64_t &encoded) {
	constexpr std::uint64_t names = 20000;
	d::DirectoryUpdate directory;
	directory.to.cluster.fill('c');
	directory.to.position = names;
	for (std::uint64_t i = 0; i < names; ++i) {
		char name[24];
		std::snprintf(name, sizeof name, "app.n%06llu", static_cast<unsigned long long>(i));
		directory.entries.push_back({name, {}, true});
		directory.entries.back().incarnation.fill('i');
	}
	d::Identity update;
	update.fill('u');
	a::FramesExport exporter({}, directory, update, compress, account, std::size_t{1} << 26);
	std::vector<std::shared_ptr<const d::OwnedBuffer>> frames;
	for (unsigned turns = 0; !exporter.done(); check(++turns < 1000000)) {
		d::WorkBudget budget{65536, 16};
		exporter.step(budget);
		if (exporter.frame_ready())
			frames.push_back(exporter.take_frame());
	}
	encoded = exporter.encoded_length();
	return frames;
}
} // namespace
int main() {
	try {
		auto account = std::make_shared<d::StorageLedger>(std::size_t{1} << 28);
		const auto page = d::make_owned_buffer(account, 1)->storage_capacity() - 1;
		std::uint64_t raw = 0, encoded = 0;
		const auto plain = answer(false, account, raw);
		check(plain.size() > 2);
		for (std::size_t i = 0; i + 1 < plain.size(); ++i)
			check(plain[i]->storage_capacity() == d::OwnedFragmentEncoder::maximum_wire_bytes() + page);
		const auto packed = answer(true, account, encoded);
		check(encoded == raw && packed.size() == plain.size());
		std::size_t sent = 0, held = 0;
		for (const auto &frame : packed) {
			check(frame->storage_capacity() == frame->size() + page);
			sent += frame->size();
			held += frame->storage_capacity();
		}
		std::cout << "frames export: " << raw << " raw bytes in " << packed.size() << " fragments, compressed to "
				  << sent << " sent and " << held << " held\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "frames export buffers: " << error.what() << '\n';
		return 1;
	}
}
