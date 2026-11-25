// Probe P5 helper: a DYLD_INSERT_LIBRARIES interposer that logs every fsync, fdatasync and
// fcntl(F_FULLFSYNC | F_BARRIERFSYNC) the process makes, from any image and any call path (the
// test seam's default calls included), to stderr as "SYS <call> <dir>/<name>". macOS only.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

extern int fdatasync(int fd);  // in libsystem_kernel; the SDK's unistd.h does not declare it

#define DYLD_INTERPOSE(replacement, replacee) \
	__attribute__((used)) static struct { const void* r; const void* e; } interpose_##replacee \
		__attribute__((section("__DATA,__interpose"))) = {(const void*)(unsigned long)&replacement, (const void*)(unsigned long)&replacee};

static void name_of(int fd, char* out, size_t size) {
	char path[MAXPATHLEN] = {0};
	if (fcntl(fd, F_GETPATH, path) != 0) { snprintf(out, size, "fd%d", fd); return; }
	char* last = strrchr(path, '/');
	if (!last || last == path) { snprintf(out, size, "%s", path); return; }
	*last = '\0';
	char* parent = strrchr(path, '/');
	snprintf(out, size, "%s/%s", parent ? parent + 1 : path, last + 1);
}
static void log_call(const char* call, int fd, int result, int error) {
	char name[512];
	name_of(fd, name, sizeof name);
	dprintf(2, "SYS %s %s%s%s\n", call, name, result < 0 ? " FAIL " : "", result < 0 ? strerror(error) : "");
}
static int logged_fsync(int fd) {
	int result = fsync(fd), error = errno;
	log_call("fsync", fd, result, error);
	errno = error;
	return result;
}
static int logged_fdatasync(int fd) {
	int result = fdatasync(fd), error = errno;
	log_call("fdatasync", fd, result, error);
	errno = error;
	return result;
}
static int logged_fcntl(int fd, int command, ...) {
	va_list args;
	va_start(args, command);
	void* argument = va_arg(args, void*);
	va_end(args);
	int result = fcntl(fd, command, argument), error = errno;
	if (command == F_FULLFSYNC || command == F_BARRIERFSYNC) { log_call(command == F_FULLFSYNC ? "full" : "barrier", fd, result, error); }
	errno = error;
	return result;
}
DYLD_INTERPOSE(logged_fsync, fsync)
DYLD_INTERPOSE(logged_fdatasync, fdatasync)
DYLD_INTERPOSE(logged_fcntl, fcntl)
