#include "daemon.hh"

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace detent_cli {
namespace {
void fail(const char* operation) { throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno)); }
int high_fd(int fd) {
	if (fd < 0 || fd >= 3) return fd;
	int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
	int error = errno;
	::close(fd);
	errno = error;
	return copy;
}
struct Fd {
	int value;
	~Fd() { if (value >= 0) ::close(value); }
};
}

std::optional<pid_t> locked_pid(const std::filesystem::path& path) {
	Fd fd{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
	if (fd.value < 0) { if (errno == ENOENT) return std::nullopt; fail("open pidfile"); }
	struct stat st{};
	if (::fstat(fd.value, &st) || !S_ISREG(st.st_mode)) throw std::runtime_error("pidfile must be a regular file");
	if (::flock(fd.value, LOCK_EX | LOCK_NB) == 0) return std::nullopt;
	if (errno != EWOULDBLOCK && errno != EAGAIN) fail("lock pidfile");
	char bytes[32];
	auto n = ::pread(fd.value, bytes, sizeof(bytes), 0);
	if (n <= 0) return std::nullopt; // startup may be writing its PID
	pid_t pid = 0;
	auto [end, error] = std::from_chars(bytes, bytes + n, pid);
	if (error != std::errc{} || end != bytes + n || pid <= 0) return std::nullopt;
	return pid;
}

ProcessLock::ProcessLock(const std::filesystem::path& path, bool system) {
	auto dir = path.parent_path();
	std::filesystem::create_directories(dir);
	struct stat st{};
	if (::lstat(dir.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) {
		throw std::runtime_error("runtime directory must be owned by the current user and not be a symlink: " + dir.string());
	}
	if (::chmod(dir.c_str(), system ? 0755 : 0700)) fail("chmod runtime directory");
	Fd fd{::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, system ? 0644 : 0600)};
	if (fd.value < 0) fail("open pidfile");
	if (::fstat(fd.value, &st) || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || st.st_nlink != 1) {
		throw std::runtime_error("pidfile must be a singly linked regular file owned by the current user");
	}
	if (::flock(fd.value, LOCK_EX | LOCK_NB)) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) return;
		fail("lock pidfile");
	}
	auto pid = std::to_string(::getpid());
	if (::ftruncate(fd.value, 0)) fail("truncate pidfile");
	if (::pwrite(fd.value, pid.data(), pid.size(), 0) != static_cast<ssize_t>(pid.size())) fail("write pidfile");
	fd_ = fd.value;
	fd.value = -1;
}
ProcessLock::~ProcessLock() { if (fd_ >= 0) ::close(fd_); }

int run_daemon(bool detach, const std::filesystem::path& log_path, const std::function<int(const Ready&)>& run,
               std::chrono::seconds startup_timeout) {
	if (!detach) return run([] {});
	int pipefd[2];
	if (::pipe(pipefd)) fail("startup pipe");
	Fd input{high_fd(pipefd[0])}, output{high_fd(pipefd[1])};
	if (input.value < 0 || output.value < 0) fail("duplicate startup pipe");
	auto pid = ::fork();
	if (pid < 0) fail("fork");
	if (pid == 0) {
		::close(input.value); input.value = -1;
		Ready ready = [&] {
			if (output.value >= 0) {
				ssize_t n;
				do { n = ::write(output.value, "R", 1); } while (n < 0 && errno == EINTR);
				if (n != 1) fail("report daemon readiness");
				::close(output.value); output.value = -1;
			}
		};
		try {
			if (::setsid() < 0) fail("setsid");
			std::filesystem::create_directories(log_path.parent_path());
			Fd log{high_fd(::open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600))};
			if (log.value < 0) fail("open daemon log");
			struct stat st{};
			if (::fstat(log.value, &st) || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || st.st_nlink != 1) throw std::runtime_error("daemon log must be a singly linked regular file owned by the current user");
			if (::dup2(log.value, STDOUT_FILENO) < 0 || ::dup2(log.value, STDERR_FILENO) < 0) fail("redirect daemon log");
			Fd null{high_fd(::open("/dev/null", O_RDONLY | O_CLOEXEC))};
			if (null.value < 0 || ::dup2(null.value, STDIN_FILENO) < 0) fail("redirect stdin");
			auto result = run(ready);
			if (result != 0 && output.value >= 0) throw std::runtime_error("daemon startup failed");
			ready();
			::_exit(result);
		} catch (const std::exception& e) {
			if (output.value >= 0) {
				std::string message = "E";
				message += e.what();
				if (message.size() > 4096) message.resize(4096);
				(void)::write(output.value, message.data(), message.size());
			}
			::_exit(1);
		}
	}
	::close(output.value); output.value = -1;
	std::string error;
	auto deadline = std::chrono::steady_clock::now() + startup_timeout;
	for (;;) {
		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
		pollfd pfd{input.value, POLLIN, 0};
		int result = remaining > 0 ? ::poll(&pfd, 1, static_cast<int>(remaining)) : 0;
		if (result < 0 && errno == EINTR) continue;
		if (result < 0) fail("wait for daemon startup");
		if (result == 0) {
			::kill(pid, SIGTERM);
			throw std::runtime_error("daemon startup timed out");
		}
		char buffer[512];
		auto n = ::read(input.value, buffer, sizeof(buffer));
		if (n < 0 && errno == EINTR) continue;
		if (n < 0) fail("read daemon startup");
		if (n == 0) break;
		error.append(buffer, n);
	}
	if (error != "R") {
		(void)::waitpid(pid, nullptr, 0);
		throw std::runtime_error(error.starts_with("E") ? error.substr(1) : "daemon exited without reporting readiness");
	}
	return 0;
}
} // namespace detent_cli
