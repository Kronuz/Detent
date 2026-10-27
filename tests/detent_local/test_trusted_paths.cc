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
bool race_armed = false, race_reached = false, race_failed = false;
unsigned race_calls = 0;
uid_t publisher = 0;
} // namespace
static int racing_fstatat(int directory, const char *name, struct stat *info, int flags) {
	if (race_armed && std::strcmp(name, "raced") == 0 && ++race_calls == 2) {
		race_armed = false;
		race_reached = true;
		race_failed = ::renameat(directory, name, directory, "retired-raced") != 0 ||
		              ::mkdirat(directory, name, 0700) != 0 ||
		              ::fchownat(directory, name, publisher, gid_t(-1), AT_SYMLINK_NOFOLLOW) != 0;
	}
	return ::fstatat(directory, name, info, flags);
}
// Replace the directory between opening its descriptor and checking its name.
#define fstatat racing_fstatat
#include "generation_file_access.hh"
#undef fstatat

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
	void opens(const fs::path &path) const {
		x::RecoveryFile held(x::open_publisher_directory(path, publisher));
		struct stat actual{}, expected{};
		check(::fstat(held.fd, &actual) == 0 && ::stat(target.c_str(), &expected) == 0);
		check(actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino);
	}
};
void exercise(const fs::path &parent) {
	Fixture fixture(parent);
	const auto &root = fixture.root;
	fs::create_directory_symlink(fixture.target, root / "absolute");
	fixture.opens(root / "absolute");
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
#ifdef __APPLE__
	check(fs::create_directory(root / "acl"));
	check(::chown((root / "acl").c_str(), publisher, gid_t(-1)) == 0);
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
		check(argc == 3 && ::geteuid() == 0);
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
