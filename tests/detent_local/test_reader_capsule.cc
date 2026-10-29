#include "paged_root_prefix.hh"
#include "prepared_reader_fixture.hh"
#include "reader_capsule.hh"
#include <sys/file.h>

namespace {
using namespace prepared_test;
template <class Action> void rejected(Action action) {
	bool refused = false;
	try {
		action();
	} catch (const l::GenerationUnavailable &) {
		throw;
	} catch (const std::exception &) {
		refused = true;
	}
	check(refused);
}
template <class Action> void unavailable(Action action) {
	bool refused = false;
	try {
		action();
	} catch (const l::GenerationUnavailable &) {
		refused = true;
	}
	check(refused);
}
std::vector<char> read_file(const std::filesystem::path &path) {
	const auto length = std::filesystem::file_size(path);
	std::vector<char> bytes(length);
	const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	check(fd >= 0 && ::pread(fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
	check(::close(fd) == 0);
	return bytes;
}
void replace(const std::filesystem::path &path, std::span<const char> bytes) {
	std::filesystem::remove(path);
	write_file(path, bytes);
}
void seal(std::array<char, x::reader_capsule_bytes> &bytes) {
	const auto digest = p::physical_commitment(std::span(bytes).first(592));
	std::copy(digest.begin(), digest.end(), bytes.begin() + 592);
}
void equivalent(const x::PreparedGeneration &a, const x::PreparedGeneration &b) {
	check(a.descriptor == b.descriptor && a.directory_identity == b.directory_identity);
	check(a.view.get("a", "k").value() == b.view.get("a", "k").value());
	check(a.view.eval("b", "k").value() == b.view.eval("b", "k").value());
	check(a.view.eval("b", "k", {}, "force").value() == b.view.eval("b", "k", {}, "force").value());
	check(a.view.int_count() == b.view.int_count() && a.view.bool_count() == b.view.bool_count());
}
} // namespace
int main() {
	Fixture fixture;
	const auto metadata_path = fixture.path / x::GenerationName(fixture.generation, ".read").view();
	const auto image_path = fixture.path / x::GenerationName(fixture.generation, ".image").view();
	const auto legacy_bytes = read_file(metadata_path);
	const auto metadata = x::decode_reader_metadata(legacy_bytes);
	const auto bytes = x::encode_reader_capsule({fixture.storage, fixture.root, metadata});
	for (std::size_t length = 0; length <= bytes.size(); ++length) {
		x::validate_reader_capsule_prefix(std::span(bytes).first(length), metadata);
		x::validate_reader_capsule_prefix(std::span(bytes).first(length), metadata,
		                                  p::encode_root(fixture.root));
	}
	for (const auto offset : {0u, 8u, 12u, 16u, 79u, 80u, 159u, 399u, 400u, 432u, 448u, 516u, 519u, 520u, 591u, 592u, 623u}) {
		auto corrupt = bytes;
		corrupt[offset] ^= 1;
		rejected([&] { x::validate_reader_capsule_prefix(std::span(corrupt).first(offset + 1), metadata, p::encode_root(fixture.root)); });
	}
	check(bytes.size() == 624 && std::string_view(bytes.data(), 8) == "DTCAPS01");
	check(x::encode_reader_capsule(x::decode_reader_capsule(bytes)) == bytes);
	check(std::equal(legacy_bytes.begin(), legacy_bytes.end(), bytes.begin() + 400));
	const auto storage_bytes = l::encode_storage_identity(fixture.storage);
	const auto image_bytes = p::encode_root(fixture.root);
	check(std::equal(storage_bytes.begin(), storage_bytes.end(), bytes.begin() + 16));
	check(std::equal(image_bytes.begin(), image_bytes.end(), bytes.begin() + 80));
	for (std::size_t length = 0; length < bytes.size(); ++length)
		rejected([&] { x::decode_reader_capsule(std::span(bytes).first(length)); });
	std::array<char, x::reader_capsule_bytes + 1> oversized{};
	std::copy(bytes.begin(), bytes.end(), oversized.begin());
	rejected([&] { x::decode_reader_capsule(oversized); });
	for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
		auto corrupt = bytes;
		corrupt[offset] ^= 1;
		rejected([&] { x::decode_reader_capsule(corrupt); });
		if (offset < 592) {
			seal(corrupt);
			rejected([&] { x::decode_reader_capsule(corrupt); });
		}
	}
	for (unsigned field = 0; field != 4; ++field) {
		auto corrupt = bytes;
		auto changed = metadata;
		if (field == 0)
			changed.storage[0] ^= 1;
		if (field == 1)
			changed.root_checksum[0] ^= 1;
		if (field == 2)
			++changed.counts.ints;
		if (field == 3)
			changed.physical_bytes = p::root_bytes;
		const auto encoded = x::encode_reader_metadata(changed);
		std::copy(encoded.begin(), encoded.end(), corrupt.begin() + 400);
		seal(corrupt);
		rejected([&] { x::decode_reader_capsule(corrupt); });
		rejected([&] { x::encode_reader_capsule({fixture.storage, fixture.root, changed}); });
	}
	{
		auto large = x::decode_reader_capsule(bytes);
		large.root.records = 5'000'000;
		large.root.pages = large.root.records + large.root.namespaces;
		large.root.bytes = large.root.pages * p::page_header_bytes + large.root.tree.length;
		large.metadata.counts.ints = large.root.records - large.metadata.counts.bools;
		large.metadata.physical_bytes = large.root.bytes + p::root_bytes;
		large.metadata.root_checksum =
			p::physical_commitment(p::encode_root(large.root, x::paged_format_limits()));
		check(x::decode_reader_capsule(x::encode_reader_capsule(large)).root.records == 5'000'000);
	}
	const auto legacy = x::PreparedReader::open(fixture.path, ::getuid(), {});
	replace(metadata_path, bytes);
	const auto capsule = x::PreparedReader::open(fixture.path, ::getuid(), {});
	equivalent(legacy, capsule);
	check(!capsule.view.eval("b", "k", {}, "force").value());
	auto future = fixture.root.cursor;
	++future.revision;
	rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}, future); });
	auto wrong_identity = fixture.root.cursor;
	wrong_identity.scope[0] ^= 1;
	rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}, wrong_identity); });
	for (unsigned field = 0; field != 8; ++field) {
		auto limits = l::ViewLimits::prepared();
		if (field == 0)
			limits.bytes = metadata.physical_bytes - 1;
		if (field == 1)
			limits.records = 1;
		if (field == 2)
			limits.namespaces = 1;
		if (field == 3)
			limits.strings = 0;
		if (field == 4)
			limits.overrides = 0;
		if (field == 5)
			limits.pages = fixture.root.pages - 1;
		if (field == 6)
			limits.segments = fixture.root.segments - 1;
		if (field == 7)
			limits.records = 0;
		rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), limits); });
	}
	// Authenticated nested records still have to bind to the selected descriptor
	// and lease.
	for (unsigned field = 0; field != 3; ++field) {
		auto changed = x::decode_reader_capsule(bytes);
		if (field == 0)
			changed.metadata.generation[0] ^= 1;
		if (field == 1) {
			changed.storage[0] ^= 1;
			changed.metadata.storage = changed.storage;
		}
		if (field == 2) {
			++changed.root.cursor.revision;
			changed.metadata.root_checksum = p::physical_commitment(p::encode_root(changed.root));
		}
		replace(metadata_path, x::encode_reader_capsule(changed));
		rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	}
	replace(metadata_path, bytes);
	const auto lease_path = fixture.path / x::GenerationName(fixture.generation, ".lease").view();
	int lease = ::open(lease_path.c_str(), O_RDONLY | O_CLOEXEC);
	check(lease >= 0 && ::flock(lease, LOCK_EX | LOCK_NB) < 0);
	check(::close(lease) == 0);
	// Capsule authority deliberately excludes both standalone original records.
	for (const auto &original : {fixture.path / "storage.meta", image_path}) {
		const auto saved = read_file(original);
		for (unsigned damage = 0; damage != 5; ++damage) {
			std::filesystem::remove(original);
			if (damage == 1)
				write_file(original, std::array<char, 1>{'x'});
			if (damage == 2)
				std::filesystem::create_symlink(metadata_path, original);
			if (damage == 3) {
				write_file(original, saved);
				check(::chmod(original.c_str(), 0666) == 0);
			}
			if (damage == 4) {
				auto changed = saved;
				changed.back() ^= 1;
				write_file(original, changed);
			}
			equivalent(legacy, x::PreparedReader::open(fixture.path, ::getuid(), {}));
			replace(metadata_path, legacy_bytes);
			rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
			std::filesystem::remove(metadata_path);
			rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
			replace(metadata_path, bytes);
		}
		replace(original, saved);
	}
	std::filesystem::remove(metadata_path);
	unavailable([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	// Unsafe or malformed capsule metadata never falls back to the valid
	// originals.
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		replace(metadata_path, std::span(bytes).first(length));
		rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	}
	replace(metadata_path, oversized);
	rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	replace(metadata_path, bytes);
	check(::chmod(metadata_path.c_str(), 0666) == 0);
	rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	std::filesystem::remove(metadata_path);
	std::filesystem::create_symlink(image_path, metadata_path);
	rejected([&] { x::PreparedReader::open(fixture.path, ::getuid(), {}); });
	replace(metadata_path, bytes);
	const auto payload =
		fixture.path / x::SegmentNames(fixture.definitions[0].segment).payload.data();
	const int fd = ::open(payload.c_str(), O_RDWR | O_CLOEXEC);
	check(fd >= 0);
	char byte = 0;
	check(::pread(fd, &byte, 1, fixture.definitions[0].offset) == 1);
	byte ^= 1;
	check(::pwrite(fd, &byte, 1, fixture.definitions[0].offset) == 1 && ::close(fd) == 0);
	const auto lazy = x::PreparedReader::open(fixture.path, ::getuid(), {});
	const auto result = lazy.view.get("a", "k");
	check(!result && result.error().category == detent::ErrorCategory::SourceFailure);
}
