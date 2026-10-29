#include "generation_file_access.hh"
#include <array>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace x = detent::local::detail;
void check(bool value) {
  if (!value)
    throw std::runtime_error("ancestor ownership invariant");
}
unsigned descriptors() {
  unsigned result = 0;
  for (int fd = 0; fd != 1024; ++fd)
    result += ::fcntl(fd, F_GETFD) >= 0;
  return result;
}
struct Limits {
  rlimit original{};
  Limits() {
    check(::getrlimit(RLIMIT_NOFILE, &original) == 0);
    if (original.rlim_cur < 512 && original.rlim_max >= 512) {
      auto next = original;
      next.rlim_cur = 512;
      check(::setrlimit(RLIMIT_NOFILE, &next) == 0);
    }
  }
  ~Limits() { ::setrlimit(RLIMIT_NOFILE, &original); }
};
struct Fixture {
  fs::path root;
  Fixture()
      : root(fs::current_path() / ".scratch" /
             ("ancestor-" + std::to_string(::getpid()))) {
    check(fs::create_directories(root));
    check(::chmod(root.c_str(), 0700) == 0);
  }
  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::fprintf(stderr, "ancestor cleanup: %s\n", error.message().c_str());
  }
};
void opens(const fs::path &path, const fs::path &expected) {
  const auto before = descriptors();
  {
    x::RecoveryFile file(x::open_publisher_directory(path, ::getuid()));
    struct stat actual{}, wanted{};
    check(::fstat(file.fd, &actual) == 0 &&
          ::stat(expected.c_str(), &wanted) == 0);
    check(actual.st_dev == wanted.st_dev && actual.st_ino == wanted.st_ino);
    check((::fcntl(file.fd, F_GETFD) & FD_CLOEXEC) != 0);
  }
  check(descriptors() == before);
}
template <class Exception> void rejects(const fs::path &path) {
  const auto before = descriptors();
  bool rejected = false;
  try {
    x::RecoveryFile file(x::open_publisher_directory(path, ::getuid()));
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected && descriptors() == before);
}
int main() {
  try {
    Limits limits;
    Fixture fixture;
    const auto target = fixture.root / "target";
    check(fs::create_directories(fixture.root / "a/b"));
    check(fs::create_directory(target));
    opens(target, target);
    opens(fs::relative(target), target);
    opens(fixture.root / "a/b/../../target", target);
    opens(fs::path("/../../") / target.relative_path(), target);
    rejects<std::invalid_argument>(fs::path{});
    rejects<std::invalid_argument>(fs::path(std::string("bad\0path", 8)));
    rejects<detent::local::GenerationUnavailable>(fixture.root / "a/missing");
    check(::chmod((fixture.root / "a/b").c_str(), 0777) == 0);
    rejects<std::runtime_error>(fixture.root / "a/b");
    check(::chmod((fixture.root / "a/b").c_str(), 0700) == 0);
    fs::create_symlink(target, fixture.root / "alias");
    if (::getuid() == 0)
      opens(fixture.root / "alias", target);
    else
      rejects<std::runtime_error>(fixture.root / "alias");
    if (limits.original.rlim_max < 512) {
      std::fprintf(stderr, "Maximum-depth fixture needs 512 descriptors\n");
      return 77;
    }
    auto deep = fixture.root;
    std::size_t components = 0;
    for (const auto &part : deep.relative_path()) {
      (void)part;
      ++components;
    }
    check(components < 256);
    while (components < 256) {
      deep /= "d";
      check(fs::create_directory(deep));
      ++components;
    }
    opens(deep, deep);
    check(::chmod(deep.c_str(), 0777) == 0);
    rejects<std::runtime_error>(deep);
    check(::chmod(deep.c_str(), 0700) == 0);
    check(fs::create_directory(deep / "d"));
    rejects<std::runtime_error>(deep / "d");
    std::printf("ancestor slots=%zu, depth bytes=%zu, owner bytes=%zu\n",
                std::size_t(256), sizeof(std::size_t),
                sizeof(std::array<std::optional<x::RecoveryFile>, 256>));
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
