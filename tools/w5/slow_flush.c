// Interposed fsync and fcntl(F_FULLFSYNC) that sleep SLOW_FLUSH_MS first: a loaded disk, made
// deterministic (macOS, DYLD_INSERT_LIBRARIES). Used by slow_flush_get.sh.
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static void wait_flush(void) {
	const char *ms = getenv("SLOW_FLUSH_MS");
	if (ms)
		usleep((useconds_t)atoi(ms) * 1000);
}

static int slow_fsync(int fd) {
	wait_flush();
	return fsync(fd);
}

static int slow_fcntl(int fd, int command, ...) {
	va_list args;
	va_start(args, command);
	void *argument = va_arg(args, void *);
	va_end(args);
	if (command == F_FULLFSYNC)
		wait_flush();
	return fcntl(fd, command, argument);
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *replacement, *original;
} interposed[] = {{(const void *)slow_fsync, (const void *)fsync}, {(const void *)slow_fcntl, (const void *)fcntl}};
