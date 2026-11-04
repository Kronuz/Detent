// Shared helpers of the M1 identity drivers (tools/w5): each driver prints one line per case, and the
// same source is built against the old and the new pins, so `diff` of the two outputs is the result.
#pragma once

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace w5 {

inline std::uint64_t fnv(const void* data, std::size_t size, std::uint64_t h = 0xcbf29ce484222325ULL) {
	auto p = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) {
		h = (h ^ p[i]) * 0x100000001b3ULL;
	}
	return h;
}

inline std::uint64_t fnv(std::string_view s) {
	return fnv(s.data(), s.size());
}

inline std::string hex(std::uint64_t v) {
	char b[20];
	std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
	return b;
}

// Escapes a byte string for one output line.
inline std::string show(std::string_view s, std::size_t limit = 60) {
	std::string out;
	for (std::size_t i = 0; i < s.size() && i < limit; ++i) {
		auto c = static_cast<unsigned char>(s[i]);
		if (c >= 0x20 && c < 0x7f && c != '\\') {
			out += static_cast<char>(c);
		} else {
			char b[8];
			std::snprintf(b, sizeof b, "\\x%02x", c);
			out += b;
		}
	}
	if (s.size() > limit) {
		out += "...(" + std::to_string(s.size()) + ")";
	}
	return out;
}

// Runs `fn` in a child process and returns the line it produced: a crash, a hang or an abort of the code
// under test (the old pins have such inputs) is a result, not the end of the run.
inline std::string isolated(const std::function<std::string()>& fn, unsigned seconds = 20) {
	std::fflush(nullptr);
	int fds[2];
	if (pipe(fds) != 0) {
		return "pipe-failed";
	}
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]);
		alarm(seconds);
		std::string out;
		try {
			out = fn();
		} catch (...) {
			out = "uncaught";
		}
		std::size_t done = 0;
		while (done < out.size()) {
			auto n = write(fds[1], out.data() + done, out.size() - done);
			if (n <= 0) break;
			done += static_cast<std::size_t>(n);
		}
		_exit(0);
	}
	close(fds[1]);
	std::string out;
	char buf[4096];
	for (;;) {
		auto n = read(fds[0], buf, sizeof buf);
		if (n <= 0) break;
		out.append(buf, static_cast<std::size_t>(n));
	}
	close(fds[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) {
		return WTERMSIG(status) == SIGALRM ? "hang" : "crash:signal-" + std::to_string(WTERMSIG(status));
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
		return "crash:exit-" + std::to_string(WEXITSTATUS(status));
	}
	return out;
}

// One case per line: the case id, a tab, the result. compare.py keys on the id, so a case that exists in one
// build only (a new enum member) is reported instead of shifting every later line.
inline void line(const std::string& id, const std::string& result) {
	std::printf("%s\t%s\n", id.c_str(), result.c_str());
}

} // namespace w5
