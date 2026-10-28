#include "detent/config_trust_internal.hh"
#include "detent_local/generation.hh"
#include "generation_mapping.hh"
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
bool race_armed = false, race_opened = false, race_reached = false, race_failed = false;
bool alias_race_armed = false, alias_race_reached = false, alias_race_failed = false;
const char *fault_name = nullptr;
int fault_error = 0;
const char *stat_fault_name = nullptr;
int stat_fault_error = 0;
unsigned stat_fault_at = 1, stat_calls = 0;
unsigned inspections = 0, alias_reads = 0;
uid_t publisher = 0;
} // namespace
static int racing_openat(int directory, const char *name, int flags) {
	if (fault_name && std::strcmp(name, fault_name) == 0) {
		errno = fault_error;
		return -1;
	}
	const auto result = ::openat(directory, name, flags);
	if (race_armed && result >= 0 && std::strcmp(name, "raced") == 0)
		race_opened = true;
	return result;
}
static int racing_fstatat(int directory, const char *name, struct stat *info, int flags) {
	if (stat_fault_name && std::strcmp(name, stat_fault_name) == 0 && ++stat_calls == stat_fault_at) {
		errno = stat_fault_error;
		return -1;
	}
	if (fault_name && std::strcmp(name, fault_name) == 0)
		++inspections;
	if (race_armed && race_opened && std::strcmp(name, "raced") == 0) {
		race_armed = false;
		race_reached = true;
		race_failed = ::renameat(directory, name, directory, "retired-raced") != 0 ||
		              ::mkdirat(directory, name, 0700) != 0 ||
		              ::fchownat(directory, name, publisher, gid_t(-1), AT_SYMLINK_NOFOLLOW) != 0;
	}
	return ::fstatat(directory, name, info, flags);
}
static ssize_t racing_readlinkat(int directory, const char *name, char *bytes, size_t size) {
	++alias_reads;
	const auto result = ::readlinkat(directory, name, bytes, size);
	if (alias_race_armed && result >= 0 && std::strcmp(name, "alias-raced") == 0) {
		alias_race_armed = false;
		alias_race_reached = true;
		alias_race_failed = ::renameat(directory, name, directory, "retired-alias-raced") != 0 ||
		                    ::symlinkat("target", directory, name) != 0;
	}
	return result;
}
// Replace the directory between opening its descriptor and checking its name.
#define openat racing_openat
#define fstatat racing_fstatat
#define readlinkat racing_readlinkat
#include "generation_file_access.hh"
#undef readlinkat
#undef fstatat
#undef openat

namespace x = detent::local::detail;
namespace fs = std::filesystem;
namespace {
void check(bool value) {
	if (!value)
		throw std::runtime_error("trusted path invariant");
}
unsigned descriptors() {
	unsigned count = 0;
	for (int fd = 0; fd != 1024; ++fd)
		count += ::fcntl(fd, F_GETFD) >= 0;
	return count;
}
struct Fixture {
	fs::path root, target;
	explicit Fixture(const fs::path &parent)
	    : root(parent / ("trusted-paths-" + std::to_string(::getpid()))), target(root / "target") {
		check(fs::create_directory(root));
		check(::chmod(root.c_str(), 0755) == 0);
		check(fs::create_directory(target));
		check(::chown(target.c_str(), publisher, gid_t(-1)) == 0);
		check(::chmod(target.c_str(), 0700) == 0);
	}
	~Fixture() {
		std::error_code error;
		fs::remove_all(root, error);
		if (error)
			std::fprintf(stderr, "fixture cleanup: %s\n", error.message().c_str());
	}
	void rejects(const fs::path &path) const {
		bool rejected = false;
		try {
			x::RecoveryFile held(x::open_publisher_directory(path, publisher));
		} catch (const std::exception &) {
			rejected = true;
		}
		check(rejected);
	}
	void unavailable(const fs::path &path) const {
		bool missing = false;
		try {
			x::RecoveryFile held(x::open_publisher_directory(path, publisher));
		} catch (const detent::local::GenerationUnavailable &) {
			missing = true;
		}
		check(missing);
	}
	void trust_error(const fs::path &path) const {
		bool rejected = false;
		try {
			x::RecoveryFile held(x::open_publisher_directory(path, publisher));
		} catch (const detent::local::GenerationUnavailable &) {
			throw;
		} catch (const std::runtime_error &) {
			rejected = true;
		}
		check(rejected);
	}
	void opens(const fs::path &path) const {
		x::RecoveryFile held(x::open_publisher_directory(path, publisher));
		struct stat actual{}, expected{};
		check(::fstat(held.fd, &actual) == 0 && ::stat(target.c_str(), &expected) == 0);
		check(actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino);
		check((::fcntl(held.fd, F_GETFD) & FD_CLOEXEC) != 0);
	}
};
void ordinary(const fs::path &parent) {
	fs::create_directories(parent);
	Fixture fixture(parent);
	fixture.opens(fixture.target);
	fixture.opens(fs::relative(fixture.target));
	fixture.opens(fixture.root / "./target");
	check(fs::create_directory(fixture.root / "child"));
	fixture.opens(fixture.root / "child/../target");
	fixture.opens(fs::path("/../../") / fixture.target.relative_path());
	fixture.rejects(fs::path{});
	fixture.rejects(fs::path(std::string("bad\0path", 8)));
	fixture.unavailable(fixture.root / "missing/target");
	{
		x::RecoveryFile file(::open((fixture.root / "file").c_str(), O_CREAT | O_WRONLY, 0600));
		check(file.fd >= 0);
	}
	fixture.rejects(fixture.root / "file");
	auto expanded = fixture.root;
	for (unsigned i = 0; i != 257; ++i)
		expanded /= ".";
	fixture.rejects(expanded / "target");
	const auto before = descriptors();
	for (const int error : {EACCES, EIO, EINTR, EMFILE, ELOOP, ENOTDIR, EMLINK}) {
		fault_name = "target";
		fault_error = error;
		inspections = alias_reads = 0;
		fixture.rejects(fixture.target);
		check(inspections == (error == ELOOP || error == ENOTDIR || error == EMLINK ? 1u : 0u));
		check(alias_reads == 0);
		fault_name = nullptr;
		check(descriptors() == before);
	}
	fault_name = "target";
	fault_error = ENOENT;
	inspections = alias_reads = 0;
	fixture.unavailable(fixture.target);
	check(inspections == 0 && alias_reads == 0);
	fault_name = nullptr;
	check(descriptors() == before);
	for (const int error : {EACCES, EIO, EINTR, ENOENT}) {
		stat_fault_name = "target";
		stat_fault_error = error;
		stat_calls = 0;
		fixture.trust_error(fixture.target);
		check(stat_calls == 1);
		stat_fault_name = nullptr;
		check(descriptors() == before);
	}
}
void exercise(const fs::path &parent) {
	Fixture fixture(parent);
	const auto &root = fixture.root;
	fs::create_directory_symlink(fixture.target, root / "absolute");
	fixture.opens(root / "absolute");
	for (const int error : {ELOOP, ENOTDIR, EMLINK}) {
		fault_name = "absolute";
		fault_error = error;
		inspections = 0;
		fixture.opens(root / "absolute");
		check(inspections == 2);
		fault_name = nullptr;
	}
	stat_fault_name = "absolute";
	for (const int error : {EACCES, EIO, EINTR, ENOENT}) {
		stat_fault_error = error;
		for (const unsigned call : {1u, 2u}) {
			stat_fault_at = call;
			stat_calls = 0;
			if (error == ENOENT && call == 1)
				fixture.unavailable(root / "absolute");
			else
				fixture.trust_error(root / "absolute");
			check(stat_calls == call);
		}
	}
	stat_fault_name = nullptr;
	stat_fault_at = 1;
	fs::create_directory_symlink("missing", root / "dangling");
	fixture.unavailable(root / "dangling");
	fs::create_directory_symlink("target", root / "alias-raced");
	alias_race_armed = true;
	fixture.rejects(root / "alias-raced");
	check(alias_race_reached && !alias_race_failed);
	check(fs::create_directory(root / "parent"));
	fs::create_directory_symlink("../target", root / "parent/relative");
	fixture.opens(root / "parent/relative");
	fixture.opens(root / "parent/../target");
	fs::create_directory_symlink("parent/relative", root / "combined");
	fixture.opens(root / "combined");
	for (unsigned i = 0; i != 40; ++i)
		fs::create_directory_symlink(i == 39 ? "target" : "link-" + std::to_string(i + 1),
		                             root / ("link-" + std::to_string(i)));
	fixture.opens(root / "link-0");
	check(fs::remove(root / "link-39"));
	fs::create_directory_symlink("link-40", root / "link-39");
	fs::create_directory_symlink("target", root / "link-40");
	fixture.rejects(root / "link-0");
	fs::create_directory_symlink("loop", root / "loop");
	fixture.rejects(root / "loop");
	fs::create_directory_symlink("target", root / "user-link");
	check(::lchown((root / "user-link").c_str(), publisher ? publisher : 1, gid_t(-1)) == 0);
	fixture.rejects(root / "user-link");
	check(fs::create_directory(root / "writable"));
	check(::chmod((root / "writable").c_str(), 0777) == 0);
	fs::create_directory_symlink(fixture.target, root / "writable/alias");
	fixture.rejects(root / "writable/alias");
	check(fs::create_directory(root / "foreign"));
	check(::chown((root / "foreign").c_str(), publisher == 1 ? 2 : 1, gid_t(-1)) == 0);
	fixture.rejects(root / "foreign");
	auto expanded = root;
	for (unsigned i = 0; i != 257; ++i)
		expanded /= ".";
	expanded /= "target";
	fixture.rejects(expanded);
#if defined(__APPLE__) || defined(__FreeBSD__)
	check(fs::create_directory(root / "acl"));
	check(::chown((root / "acl").c_str(), publisher, gid_t(-1)) == 0);
#ifdef __APPLE__
	const auto child = ::fork();
	check(child >= 0);
	if (!child) {
		::execl("/bin/chmod", "chmod", "+a", "everyone allow write", (root / "acl").c_str(), nullptr);
		::_exit(127);
	}
	int status = 0;
	while (::waitpid(child, &status, 0) < 0)
		check(errno == EINTR);
	check(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#else
	x::RecoveryFile acl_directory(::open((root / "acl").c_str(), O_RDONLY | O_DIRECTORY));
	check(acl_directory.fd >= 0);
	const auto extended = ::fpathconf(acl_directory.fd, _PC_ACL_EXTENDED);
	const auto nfs4 = ::fpathconf(acl_directory.fd, _PC_ACL_NFS4);
	std::printf("FreeBSD ACL capabilities: extended=%ld nfs4=%ld\n", extended, nfs4);
	check(extended == 1 && nfs4 == 0);
	acl_t acl = ::acl_from_text("user::rwx,user:65534:r-x,group::---,mask::r-x,other::---");
	check(acl != nullptr);
	const bool installed = ::acl_set_file((root / "acl").c_str(), ACL_TYPE_ACCESS, acl) == 0;
	const bool freed = ::acl_free(acl) == 0;
	check(installed && freed);
#endif
	struct stat acl_mode{};
	check(::stat((root / "acl").c_str(), &acl_mode) == 0 && !(acl_mode.st_mode & 0022));
	fixture.rejects(root / "acl");
#endif
	check(fs::create_directory(root / "raced"));
	check(::chown((root / "raced").c_str(), publisher, gid_t(-1)) == 0);
	race_armed = true;
	fixture.rejects(root / "raced");
	check(race_reached && !race_failed);
	const auto before = descriptors();
	for (unsigned i = 0; i != 100; ++i) {
		fixture.rejects(root / "loop");
		fixture.rejects(root / "writable/alias");
	}
	check(descriptors() == before);
}
} // namespace
int main(int argc, char **argv) {
	try {
		check(argc == 3);
		if (std::strcmp(argv[1], "--ordinary") == 0) {
			publisher = ::getuid();
			ordinary(fs::absolute(argv[2]));
			return 0;
		}
		check(::geteuid() == 0);
		const std::string_view uid(argv[1]);
		const auto [end, error] = std::from_chars(uid.data(), uid.data() + uid.size(), publisher);
		check(error == std::errc{} && end == uid.data() + uid.size());
		exercise(fs::absolute(argv[2]));
		return 0;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
