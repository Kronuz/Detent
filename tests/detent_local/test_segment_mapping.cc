#include "detent_local/paged_layout.hh"
#include "segment_mapping.hh"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace detent;
void require(bool value) {
	if (!value)
		throw std::runtime_error("segment mapping test failed");
}
template <class Error, class F> void rejects(F call) {
	try {
		call();
	} catch (const Error &) {
		return;
	}
	throw std::runtime_error("invalid segment mapping accepted");
}
int main() {
	auto directory =
		std::filesystem::current_path() / ".scratch" / ("segment-mapping-" + std::to_string(::getpid()));
	std::filesystem::create_directories(directory);
	auto path = directory / "segment";
	std::string bytes(81, 'x');
	{
		std::ofstream out(path, std::ios::binary);
		out << bytes;
		require(bool(out));
	}
	require(::chmod(path.c_str(), 0600) == 0);
	int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	require(fd >= 0);
	local::SegmentOwnership ownership;
	ownership.storage.fill('s');
	ownership.segment.fill('g');
	ownership.length = bytes.size();
	ownership.checksum = local::paged::physical_commitment(bytes);
	auto account = std::make_shared<distribution::StorageLedger>(1024 * 1024);
	auto controls = std::make_shared<distribution::StorageLedger>(1024 * 1024);
	distribution::RetirementRuntime runtime{controls, 1};
	auto domain = runtime.domain(account, 4);
	auto baseline = account->used();
	auto mapping = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), ownership);
	require(mapping->bytes().size() == bytes.size());
	require(std::string_view(mapping->bytes().data(), mapping->bytes().size()) == bytes);
	require(mapping->identity().inode != 0 && mapping->ownership() == ownership);
	auto occupied = account->used();
	auto wrong = ownership;
	++wrong.length;
	rejects<std::invalid_argument>(
		[&] { auto invalid = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), wrong); });
	require(account->used() == occupied);
	require(::chmod(path.c_str(), 0620) == 0);
	rejects<std::invalid_argument>([&] {
		auto invalid = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), ownership);
	});
	require(::chmod(path.c_str(), 0600) == 0);
	auto link = directory / "hardlink";
	std::filesystem::create_hard_link(path, link);
	rejects<std::invalid_argument>([&] {
		auto invalid = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), ownership);
	});
	std::filesystem::remove(link);
	auto short_admission = account->admit(account->limit() - account->used() - (occupied - baseline - 1));
	auto pressured = account->used();
	rejects<distribution::StoragePressure>([&] {
		auto invalid = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), ownership);
	});
	require(account->used() == pressured);
	short_admission.reset();
	auto pressure = account->admit(account->limit() - account->used());
	rejects<distribution::StoragePressure>([&] {
		auto invalid = domain.make<local::detail::SegmentMapping>(account, fd, ::getuid(), ownership);
	});
	pressure.reset();
	auto pinned = mapping;
	mapping.reset();
	require(::close(fd) == 0);
	std::filesystem::remove(path);
	require(pinned->bytes()[0] == 'x');
	auto drain = domain.close();
	require(!drain.done());
	pinned.reset();
	drain.wait();
	// Closed domain bookkeeping remains admitted until its own destruction.
	require(account->used() == baseline);
	std::filesystem::remove(directory);
}
