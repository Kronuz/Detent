#include "paged_disk_fixture.hh"
#include "paged_large_fixture.hh"
#include "snapshot_source.hh"
std::string encode(std::shared_ptr<d::SnapshotSource> source, bool tiny) {
	d::SnapshotEncoder encoder(std::move(source));
	std::string wire;
	unsigned turns = 0;
	while (!encoder.done()) {
		d::WorkBudget b{tiny ? 1u : 8192u, tiny ? 1u : 16u};
		encoder.step(b);
		if (encoder.chunk_ready())
			wire.append(encoder.take_chunk()->view());
		require(++turns < 10000000);
	}
	require(encoder.encoded_length() == wire.size());
	return wire;
}
std::shared_ptr<const d::ValidatedSnapshot> small(unsigned count) {
	std::string wire;
	auto n = [&](std::uint64_t v) { m::AppendPacker(wire).pack_uint64(v); };
	auto a = [&](unsigned v) { m::AppendPacker(wire).pack_array(v); };
	auto text = [&](std::string_view v) { m::AppendPacker(wire).pack_str(v); };
	a(7);
	n(1);
	n(1);
	a(2);
	n(1);
	n(1);
	a(5);
	const auto id = cursor();
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::AppendPacker(wire).pack_bin({field->data(), field->size()});
	m::AppendPacker(wire).pack_bin({id.scope.data(), id.scope.size()});
	n(id.revision);
	a(4);
	n(count ? 5 : 0);
	n(count);
	n(count ? 2 : 0);
	n(count ? 38 : 0);
	std::vector<std::string> names =
		count ? std::vector<std::string>{"a", "app", "app.x", "m", "z"} : std::vector<std::string>{};
	a(names.size());
	for (unsigned i = 0; i < names.size(); ++i) {
		a(2);
		text(names[i]);
		n(i + 1);
	}
	a(count);
	if (count) {
		const std::array<std::string_view, 4> keys{"a:b", "app:c", "app.x:a", "z:q"};
		for (unsigned i = 0; i < 4; ++i) {
			const auto ns = i == 3 ? 4 : i;
			a(4);
			n(ns);
			text(keys[i]);
			n(ns + 1);
			if (i != 1) {
				a(2);
				n(1);
				m::AppendPacker(wire).pack_int64(-1);
			} else {
				a(4);
				n(0);
				wire.push_back('\xc3');
				n(25);
				a(2);
				a(3);
				text("beta");
				n(0);
				wire.push_back('\xc2');
				a(3);
				text("tést");
				n(1);
				n(17);
			}
		}
	}
	d::SnapshotDecoder decoder(id, names);
	auto input = std::span<const char>(wire.data(), wire.size());
	while (!input.empty()) {
		d::WorkBudget b{1, 1};
		decoder.feed(input, b);
	}
	return std::make_shared<const d::ValidatedSnapshot>(decoder.finish());
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("paged-export-" + std::to_string(::getpid()));
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() {
			std::error_code e;
			std::filesystem::remove_all(path, e);
		}
	} cleanup{directory};
	for (unsigned count : {0u, 2u, 4u, 20000u}) {
		auto source =
			std::make_shared<const d::ValidatedSnapshot>(decode(count == 2 ? fixture() : many(count)));
		auto output = std::make_shared<d::StorageLedger>(4 * 1024 * 1024);
		const auto expected = encode(d::make_snapshot_source(source, output), count < 100);
		require(output->used() == 0);
		l::StorageIdentity storage{}, generation{};
		storage.fill('s');
		generation.fill('g');
		l::detail::PagedSnapshotBuilder builder(source, storage, generation);
		while (!builder.done()) {
			l::ValidationBudget b;
			builder.step(b);
		}
		auto image = builder.take();
		install(directory, image, storage);
		auto projection = l::detail::make_snapshot_source(l::recover_generation(directory), output);
		std::filesystem::remove_all(directory);
		require(encode(projection, count == 2) == expected);
		require(encode(projection, count == 2) == expected);
		projection.reset();
		require(output->used() == 0);
		std::vector<std::string> names;
		for (const auto &ns : source->namespaces())
			names.emplace_back(ns.name);
		d::SnapshotDecoder decoder(source->cursor(), names);
		auto input = std::span<const char>(expected.data(), expected.size());
		while (!input.empty()) {
			d::WorkBudget b;
			decoder.feed(input, b);
		}
		const auto roundtrip = decoder.finish();
		require(roundtrip.records().size() == count && roundtrip.cursor() == image.root().cursor);
	}
}
