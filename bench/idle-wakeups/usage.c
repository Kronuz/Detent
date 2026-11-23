#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

int main(int argc, char **argv) {
	struct rusage_info_v4 usage;
	if (argc != 2 || proc_pid_rusage(atoi(argv[1]), RUSAGE_INFO_V4,
							(rusage_info_t *)&usage))
		return 1;
	printf("user_ns=%llu system_ns=%llu package_idle=%llu interrupt=%llu\n",
		   usage.ri_user_time, usage.ri_system_time, usage.ri_pkg_idle_wkups,
		   usage.ri_interrupt_wkups);
	return 0;
}
