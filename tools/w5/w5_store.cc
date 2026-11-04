// Wire frames of the distribution layer (raw and zstd) and local images built from the benchmark corpus, written
// to the directory given as argv[1] so the old and the new build's files can be compared byte for byte. The
// fixtures are those of tests/detent_distribution/test_encoded_update.cc.
#include "common.hh"
#include "detent_distribution/encoded_update.hh"
#include "detent_distribution/framing.hh"
#include "detent_distribution/messagepack.hh"
#include "detent_distribution/update_cache.hh"
#include "detent_local/builder.hh"
#include "detent_local/reader.hh"
#include "local_corpus.hh"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace d = detent::distribution;
namespace m = d::messagepack;
namespace l = detent::local;

namespace {

d::Cursor cursor(std::uint64_t revision) {
	d::Cursor result;
	result.cluster.fill('c');
	result.group.fill('g');
	result.restore_epoch.fill('e');
	result.scope.fill('s');
	result.revision = revision;
	return result;
}

void put_cursor(std::string &out, d::Cursor id) {
	m::array(out, 5);
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(id.scope.data(), id.scope.size()), true);
	m::unsigned_integer(out, id.revision);
}

std::string delta(bool progress, std::uint64_t base = 42, std::uint64_t target = 45) {
	std::string out;
	m::array(out, progress ? 5 : 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, progress ? 3 : 2);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	put_cursor(out, cursor(base));
	put_cursor(out, cursor(target));
	if (!progress) {
		m::array(out, 4);
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, 1);
		m::unsigned_integer(out, 0);
		m::unsigned_integer(out, 8);
		m::array(out, 1);
		m::array(out, 3);
		m::unsigned_integer(out, target - 1);
		m::array(out, 1);
		m::array(out, 2);
		m::string(out, "app");
		m::unsigned_integer(out, target - 1);
		m::array(out, 1);
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		m::string(out, "app:x");
		m::unsigned_integer(out, 1);
		out.push_back('\xc0');
	}
	return out;
}

std::string snapshot() {
	std::string out;
	m::array(out, 7);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 2);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 1);
	m::array(out, 5);
	const auto id = cursor(42);
	for (const auto *field : {&id.cluster, &id.group, &id.restore_epoch})
		m::string(out, std::string_view(field->data(), field->size()), true);
	m::string(out, std::string_view(id.scope.data(), id.scope.size()), true);
	m::unsigned_integer(out, id.revision);
	m::array(out, 4);
	m::unsigned_integer(out, 1);
	m::unsigned_integer(out, 3000);
	m::unsigned_integer(out, 0);
	m::unsigned_integer(out, 3000 * 34 + 3);
	m::array(out, 1);
	m::array(out, 2);
	m::string(out, "app");
	m::unsigned_integer(out, 42);
	m::array(out, 3000);
	for (unsigned i = 0; i < 3000; ++i) {
		m::array(out, 4);
		m::unsigned_integer(out, 0);
		char key[35];
		std::snprintf(key, sizeof(key), "app:%030u", i);
		m::string(out, key);
		m::unsigned_integer(out, 42);
		m::array(out, 2);
		m::unsigned_integer(out, 1);
		m::signed_integer(out, i);
	}
	return out;
}

d::ValidatedSnapshot decode(std::string_view bytes, std::shared_ptr<d::StorageLedger> account) {
	d::Limits limits;
	limits.storage_account = account;
	d::SnapshotDecoder decoder(cursor(42), {"app"}, limits);
	auto input = std::span(bytes.data(), bytes.size());
	while (!input.empty()) {
		d::WorkBudget budget;
		decoder.feed(input, budget);
	}
	return decoder.finish();
}

void save(const std::filesystem::path &directory, const std::string &name, std::string_view bytes) {
	std::ofstream out(directory / name, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	w5::line("file " + name, std::to_string(bytes.size()) + " " + w5::hex(w5::fnv(bytes)));
}

void frames(const std::filesystem::path &directory, const std::string &label, const d::EncodedUpdate &update) {
	for (bool compressed : {false, true}) {
		for (std::size_t i = 0; i < update.fragments(); ++i) {
			auto frame = update.frame(i, compressed);
			save(directory, label + (compressed ? "-zstd-" : "-raw-") + std::to_string(i) + ".frame",
				 std::string_view(frame->view().data(), frame->size()));
		}
	}
}

} // namespace

int main(int argc, char **argv) {
	try {
		if (argc != 2)
			throw std::invalid_argument("usage: w5_store OUTPUT_DIRECTORY");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		auto root = std::make_shared<d::StorageLedger>(256 * 1024 * 1024);
		auto cache = std::make_shared<d::StorageLedger>(root, 128 * 1024 * 1024);
		auto publication = std::make_shared<d::StorageLedger>(root, 64 * 1024 * 1024);
		auto baseline = std::make_shared<d::StorageLedger>(root, 16 * 1024 * 1024);
		d::RetirementRuntime runtime(baseline, 2);
		{
			auto domain = runtime.domain(cache, 16);
			auto input_domain = runtime.domain(publication, 16);
			const auto wire = snapshot();
			auto input = decode(wire, publication);
			d::Identity identity;
			identity.fill('u');
			auto stage = domain.make<d::UpdateStage>(
				domain, d::make_snapshot_source(std::move(input), input_domain, cache), identity);
			while (!stage->done()) {
				d::WorkBudget budget{1, 1};
				stage->step(budget);
			}
			input_domain.close().wait();
			auto update = stage->take();
			stage.reset();
			frames(directory, "snapshot", *update);
			update.reset();
			domain.close().wait();
		}
		for (bool progress : {false, true}) {
			auto domain = runtime.domain(cache, 16);
			auto input_domain = runtime.domain(publication, 16);
			const auto wire = delta(progress);
			d::Limits limits;
			limits.storage_account = publication;
			d::DeltaDecoder decoder(cursor(42), {"app"}, limits, progress ? d::UpdateKind::Progress : d::UpdateKind::Delta);
			auto remaining = std::span(wire.data(), wire.size());
			while (!remaining.empty()) {
				d::WorkBudget budget;
				decoder.feed(remaining, budget);
			}
			auto stage = domain.make<d::UpdateStage>(
				domain, d::make_delta_source(decoder.finish(), 1, input_domain, cache), cursor(42).cluster);
			while (!stage->done()) {
				d::WorkBudget budget{1, 1};
				stage->step(budget);
			}
			auto update = stage->take();
			frames(directory, progress ? "progress" : "delta", *update);
			update.reset();
			stage.reset();
			domain.close().wait();
			input_domain.close().wait();
		}
		for (std::uint32_t count : {1u, 7u, 1000u, 100000u}) {
			auto corpus = detent::bench::local_corpus(count);
			d::SnapshotDecoder decoder(corpus.cursor, {"app"});
			auto input = std::span<const char>(corpus.wire);
			while (!input.empty()) {
				d::WorkBudget b{8192, 128};
				decoder.feed(input, b);
			}
			auto source = std::make_shared<d::ValidatedSnapshot>(decoder.finish());
			l::GenerationId generation;
			generation.fill('a');
			l::ImageBuilder builder(source, generation);
			while (!builder.done()) {
				l::ValidationBudget b;
				builder.step(b);
			}
			auto bytes = builder.bytes();
			save(directory, "image-" + std::to_string(count) + ".bin", std::string_view(bytes->data(), bytes->size()));
		}
		return 0;
	} catch (const std::exception &e) {
		std::fprintf(stderr, "w5_store: %s\n", e.what());
		return 1;
	}
}
