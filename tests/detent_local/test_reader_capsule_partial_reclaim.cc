#include "detent_local/disk_store.hh"
#include "prepared_reader_fixture.hh"
#include "reader_capsule.hh"
#include <completion/native_completion.h>
#include <sys/file.h>

using namespace prepared_test;
namespace {
l::GenerationId identity(unsigned value) {
	l::GenerationId result{};
	result[0] = 'p';
	result[14] = static_cast<char>(value >> 8);
	result[15] = static_cast<char>(value);
	return result;
}
void drive(const std::shared_ptr<l::DiskStore> &store) {
	for (unsigned turns = 0;; ++turns) {
		check(turns < 2'000'000);
		store->advance();
		auto job = store->operation();
		if (!job) {
			if (store->needs_work())
				continue;
			break;
		}
		job->submitted();
		check(job->complete(kronuz::journal::detail::execute_primitive(job->io(), job->request())));
	}
	if (store->error())
		std::rethrow_exception(store->error());
	if (const auto error = store->collection_error()) {
		try {
			std::rethrow_exception(error);
		} catch (const std::runtime_error &failure) {
			// Partial original roots conservatively stop segment collection.
			check(std::string_view(failure.what()) == "unsafe named paged root");
		}
	}
}
l::DiskUsage census(const std::filesystem::path &path) {
	l::DiskUsage result;
	for (const auto &entry : std::filesystem::directory_iterator(path)) {
		result.bytes += entry.file_size();
		++result.entries;
		result.generations += entry.path().extension() == ".lease";
	}
	return result;
}
} // namespace
int main() {
	Fixture f;
	write_file(f.path / "owner.lock", {});
	const auto image = p::encode_root(f.root);
	const auto digest = p::physical_commitment(image);
	std::array<char, x::reader_metadata_bytes> original{};
	const auto metadata_name = f.path / x::GenerationName(f.generation, ".read").view();
	int fd = ::open(metadata_name.c_str(), O_RDONLY | O_CLOEXEC);
	check(fd >= 0 && ::pread(fd, original.data(), original.size(), 0) == ssize_t(original.size()) &&
		  ::close(fd) == 0);
	const auto metadata = x::decode_reader_metadata(original);
	for (unsigned length = 0; length <= x::reader_capsule_bytes; ++length) {
		const auto id = identity(length);
		const auto name = l::generation_name(id);
		write_file(f.path / (name + ".lease"),
				   l::encode_generation_ownership({f.storage, id, p::root_bytes, digest, 2}));
		write_file(f.path / (name + ".image"),
				   std::span(image).first(length % (p::root_bytes + 1)));
		const auto descriptor =
			l::encode_descriptor({id, p::root_bytes, digest, f.root.cursor, f.root.profile});
		write_file(f.path / (name + ".descriptor"),
				   std::span(descriptor).first(std::min<std::size_t>(length, descriptor.size())));
		auto value = metadata;
		value.generation = id;
		const auto bytes = x::encode_reader_capsule({f.storage, f.root, value});
		write_file(f.path / (name + ".read"), std::span(bytes).first(length));
	}
	for (unsigned invalid = 0; invalid != 8; ++invalid) {
		const auto id = identity(1000 + invalid);
		const auto name = l::generation_name(id);
		write_file(f.path / (name + ".lease"),
				   l::encode_generation_ownership({f.storage, id, p::root_bytes, digest, 2}));
		write_file(f.path / (name + ".image"), image);
		auto value = metadata;
		value.generation = id;
		auto bytes = x::encode_reader_capsule({f.storage, f.root, value});
		std::size_t length = bytes.size();
		if (invalid == 0) {
			bytes[8] = 1;
			length = 9;
		}
		if (invalid == 1) {
			bytes[16] ^= 1;
			length = 17;
		}
		if (invalid == 2) {
			bytes[80 + 56] ^= 1;
			length = 80 + 57;
		}
		if (invalid == 3) {
			bytes[400 + 32] ^= 1;
			length = 400 + 33;
		}
		if (invalid == 4) {
			l::write_integer(bytes, 480, 0, 4);
			length = 488;
		}
		if (invalid == 5) {
			bytes[520] ^= 1;
			length = 521;
		}
		if (invalid == 6) {
			bytes[591] ^= 1;
			length = 592;
		}
		if (invalid == 7) {
			bytes.back() ^= 1;
		}
		write_file(f.path / (name + ".read"), std::span(bytes).first(length));
	}
	const auto pinned_path = f.path / (l::generation_name(identity(159)) + ".lease");
	const int pinned = ::open(pinned_path.c_str(), O_RDONLY | O_CLOEXEC);
	check(pinned >= 0 && ::flock(pinned, LOCK_SH | LOCK_NB) == 0);
	auto baseline = std::make_shared<d::StorageLedger>(p::segment_bytes);
	auto heap = std::make_shared<d::StorageLedger>(128 * p::segment_bytes);
	d::RetirementRuntime runtime(baseline, 1);
	auto domain = runtime.domain(heap, 128);
	l::DiskStoreOptions options;
	options.limits = {128 * p::segment_bytes, 5000, 1000};
	options.recovery_limits.retirement_domain = &domain;
	options.recovery_limits.mapping_account =
		std::make_shared<d::StorageLedger>(16 * p::segment_bytes);
	{
		auto store = l::make_disk_store(domain, f.path, heap, options);
		drive(store);
		store->request_collection();
		drive(store);
		for (unsigned length = 0; length <= x::reader_capsule_bytes; ++length) {
			const auto name = l::generation_name(identity(length));
			for (const auto suffix : {".lease", ".image", ".descriptor", ".read"})
				check(std::filesystem::exists(f.path / (name + suffix)) == (length == 159));
		}
		for (unsigned invalid = 0; invalid != 8; ++invalid) {
			const auto name = l::generation_name(identity(1000 + invalid));
			for (const auto suffix : {".lease", ".image", ".read"})
				check(std::filesystem::exists(f.path / (name + suffix)));
		}
		check(store->stats().used == census(f.path) && store->stats().used.generations == 10);
		check(::close(pinned) == 0);
		store->request_collection();
		drive(store);
		check(!std::filesystem::exists(pinned_path));
		check(store->stats().used == census(f.path) && store->stats().used.generations == 9);
		check(x::PreparedReader::open(f.path, ::getuid(), options.recovery_limits)
				  .view.get("a", "k")
				  .value() == 42);
		store->close();
		drive(store);
		check(store->drained());
	}
	domain.close().wait();
}
