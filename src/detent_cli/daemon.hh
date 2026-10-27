#pragma once

#include <filesystem>
#include <chrono>
#include <functional>
#include <optional>
#include <sys/types.h>

namespace detent_cli {

// A PID is reported only while the corresponding advisory lock is held.
std::optional<pid_t> locked_pid(const std::filesystem::path& path);
class ProcessLock {
public:
	ProcessLock(const std::filesystem::path& path, bool system);
	~ProcessLock();
	ProcessLock(const ProcessLock&) = delete;
	ProcessLock& operator=(const ProcessLock&) = delete;
	bool acquired() const noexcept { return fd_ >= 0; }
private:
	int fd_ = -1;
};

using Ready = std::function<void()>;
// Fork before constructing any threaded component. Startup errors propagate
// through a pipe; the parent returns only after ready() or an error.
int run_daemon(bool detach, const std::filesystem::path& log_path,
               const std::function<int(const Ready&)>& run,
               std::chrono::seconds startup_timeout = std::chrono::seconds(30));

} // namespace detent_cli
