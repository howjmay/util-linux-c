/*
 * taskset.c - taskset
 * Command-line utility for setting and retrieving a task's CPU affinity
 *
 * Robert Love <rml@tech9.net>		25 April 2002
 *
 * Linux kernels as of 2.5.8 provide the needed syscalls for
 * working with a task's cpu affinity.  Currently 2.4 does not
 * support these syscalls, but patches are available at:
 *
 * 	http://www.kernel.org/pub/linux/kernel/people/rml/cpu-affinity/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, v2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2004 Robert Love
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/syscall.h>

#include "cpuset.h"

static void show_usage(const char *cmd)
{
	fprintf(stderr, "taskset (%s)\n", PACKAGE_STRING);
	fprintf(stderr, "usage: %s [options] [mask | cpu-list] [pid |"\
		" cmd [args...]]\n", cmd);
	fprintf(stderr, "set or get the affinity of a process\n\n");
	fprintf(stderr, "  -p, --pid                  "
		"operate on existing given pid\n");
        fprintf(stderr, "  -c, --cpu-list             "\
		"display and specify cpus in list format\n");
	fprintf(stderr, "  -h, --help                 display this help\n");
	fprintf(stderr, "  -V, --version              "\
		"output version information\n\n");
	fprintf(stderr, "The default behavior is to run a new command:\n");
	fprintf(stderr, "  %s 03 sshd -b 1024\n", cmd);
	fprintf(stderr, "You can retrieve the mask of an existing task:\n");
	fprintf(stderr, "  %s -p 700\n", cmd);
	fprintf(stderr, "Or set it:\n");
	fprintf(stderr, "  %s -p 03 700\n", cmd);
	fprintf(stderr, "List format uses a comma-separated list instead"\
			" of a mask:\n");
	fprintf(stderr, "  %s -pc 0,3,7-11 700\n", cmd);
	fprintf(stderr, "Ranges in list format can take a stride argument:\n");
	fprintf(stderr, "  e.g. 0-31:2 is equivalent to mask 0x55555555\n\n");
}

/*
 * Number of bits in a CPU bitmask on current system
 */
static int
max_number_of_cpus(void)
{
	int n;
	int cpus = 2048;

	for (;;) {
		unsigned long buffer[longsperbits(cpus)];
		memset(buffer, 0, sizeof(buffer));
		/* the library version does not return size of cpumask_t */
		n = syscall(SYS_sched_getaffinity, 0, bytesperbits(cpus),
								&buffer);
		if (n < 0 && errno == EINVAL && cpus < 1024*1024) {
			cpus *= 2;
			continue;
		}
		return n*8;
	}
	fprintf (stderr, "cannot determine NR_CPUS; aborting");
	exit(1);
	return 0;
}

int main(int argc, char *argv[])
{
	struct bitmask *new_mask, *cur_mask;
	pid_t pid = 0;
	int opt, err;
	char *mstr;
	char *cstr;
	int c_opt = 0;
        unsigned int cpus_configured;
	int new_mask_byte_size, cur_mask_byte_size;

	struct option longopts[] = {
		{ "pid",	0, NULL, 'p' },
		{ "cpu-list",	0, NULL, 'c' },
		{ "help",	0, NULL, 'h' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	while ((opt = getopt_long(argc, argv, "+pchV", longopts, NULL)) != -1) {
		int ret = 1;

		switch (opt) {
		case 'p':
			pid = atoi(argv[argc - 1]);
			break;
		case 'c':
			c_opt = 1;
			break;
		case 'V':
			printf("taskset (%s)\n", PACKAGE_STRING);
			return 0;
		case 'h':
			ret = 0;
		default:
			show_usage(argv[0]);
			return ret;
		}
	}

	if ((!pid && argc - optind < 2)
			|| (pid && (argc - optind < 1 || argc - optind > 2))) {
		show_usage(argv[0]);
		return 1;
	}

	cpus_configured = max_number_of_cpus();

	/*
	 * cur_mask is always used for the sched_getaffinity call
	 * On the sched_getaffinity the kernel demands a user mask of
	 * at least the size of its own cpumask_t.
	 */
	cur_mask = bitmask_alloc(cpus_configured);
	if (!cur_mask) {
		fprintf (stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	cur_mask_byte_size = bitmask_nbytes(cur_mask);
	mstr = malloc(1 + cur_mask->size / 4);
	cstr = malloc(7 * cur_mask->size);

	/*
	 * new_mask is always used for the sched_setaffinity call
	 * On the sched_setaffinity the kernel will zero-fill its
	 * cpumask_t if the user's mask is shorter.
	 */
	new_mask = bitmask_alloc(cpus_configured);
	if (!new_mask) {
		fprintf (stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	new_mask_byte_size = bitmask_nbytes(new_mask);

	if (pid) {
		if (sched_getaffinity(pid, cur_mask_byte_size,
					(cpu_set_t *)cur_mask->maskp) < 0) {
			perror("sched_getaffinity");
			fprintf(stderr, "failed to get pid %d's affinity\n",
				pid);
			return 1;
		}
		if (c_opt)
			printf("pid %d's current affinity list: %s\n", pid,
				cpuset_to_cstr(cur_mask, cstr));
		else
			printf("pid %d's current affinity mask: %s\n", pid,
				cpuset_to_str(cur_mask, mstr));

		if (argc - optind == 1)
			return 0;
	}

	if (c_opt)
		err = cstr_to_cpuset(new_mask, argv[optind]);
	else
		err = str_to_cpuset(new_mask, argv[optind]);

	if (err) {
		if (c_opt)
			fprintf(stderr, "failed to parse CPU list %s\n",
				argv[optind]);
		else
			fprintf(stderr, "failed to parse CPU mask %s\n",
				argv[optind]);
		return 1;
	}

	if (sched_setaffinity(pid, new_mask_byte_size,
					(cpu_set_t *) new_mask->maskp) < 0) {
		perror("sched_setaffinity");
		fprintf(stderr, "failed to set pid %d's affinity.\n", pid);
		return 1;
	}

	if (sched_getaffinity(pid, cur_mask_byte_size,
					(cpu_set_t *)cur_mask->maskp) < 0) {
		perror("sched_getaffinity");
		fprintf(stderr, "failed to get pid %d's affinity.\n", pid);
		return 1;
	}

	if (pid) {
		if (c_opt)
			printf("pid %d's new affinity list: %s\n", pid, 
		               cpuset_to_cstr(cur_mask, cstr));
		else
			printf("pid %d's new affinity mask: %s\n", pid, 
		               cpuset_to_str(cur_mask, mstr));
	} else {
		argv += optind + 1;
		execvp(argv[0], argv);
		perror("execvp");
		fprintf(stderr, "failed to execute %s\n", argv[0]);
		return 1;
	}

	return 0;
}
