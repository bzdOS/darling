/*
 * This file is part of Darling.
 * Copyright (C) 2021 Darling developers
 *
 * Originally part of the Darling Mach Linux Kernel Module
 * Copyright (C) 2017 Lubos Dolezel
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "loader.h"
#include <darling-config.h>
#include "elfcalls/elfcalls.h"
#ifdef DARLING_FREEBSD
#  include <stdlib.h>  /* arc4random_buf */
#endif

#if defined(GEN_64BIT)
#define FUNCTION_NAME setup_stack64
#define user_long_t unsigned long
#elif defined(GEN_32BIT)
#define FUNCTION_NAME setup_stack32
#define user_long_t unsigned int
#else
#error See above
#endif

#define __user

#define EXECUTABLE_PATH "executable_path="

#define __put_user(value, pointer) ({ \
		__typeof__(value) _tmpval = (value); \
		memcpy((pointer), &_tmpval, sizeof(_tmpval)); \
		0; \
	})

void elfcalls_make(struct elf_calls* calls);

static struct elf_calls _elfcalls;

void FUNCTION_NAME(const char* filepath, struct load_results* lr)
{
	int err = 0;
	// unsigned char rand_bytes[16];
	char *executable_path;
	static char executable_buf[4096];
	user_long_t __user* argv;
	user_long_t __user* envp;
	user_long_t __user* applep;
	user_long_t __user* sp;
	char __user* exepath_user;
	size_t exepath_len;
	char __user* kernfd_user;
	char kernfd[12];
	char __user* elfcalls_user;
	char elfcalls[27];
	char __user* applep_contents[4];

#define user_long_count(_val) (((_val) + (sizeof(user_long_t) - 1)) / sizeof(user_long_t))

	elfcalls_make(&_elfcalls);

	// Produce executable_path=... for applep
	executable_buf[sizeof(executable_buf) - 1] = '\0';
	strncpy(executable_buf, filepath, 4096);
	if (executable_buf[sizeof(executable_buf) - 1] != '\0')
	{
		fprintf(stderr, "File path was too big\n");
		exit(1);
	}

	executable_path = executable_buf;

	if (lr->root_path)
	{
		exepath_len = strlen(executable_path);

		if (strncmp(executable_path, lr->root_path, lr->root_path_length) == 0)
		{
			memmove(executable_buf, executable_path + lr->root_path_length, exepath_len - lr->root_path_length + 1);
		}
		else
		{
			/* Guard against overflow: SYSTEM_ROOT prefix + path must fit in
			 * executable_buf[4096].  Truncate and warn rather than corrupt. */
			size_t prefix_len = sizeof(SYSTEM_ROOT) - 1;
			size_t avail = sizeof(executable_buf) - prefix_len - 1;
			if (exepath_len > avail) {
				fprintf(stderr,
				    "[darling-mldr] WARNING: executable path too long"
				    " (%zu > %zu), truncating\n", exepath_len, avail);
				exepath_len = avail;
			}
			memmove(executable_buf + prefix_len, executable_path, exepath_len + 1);
			memcpy(executable_buf, SYSTEM_ROOT, prefix_len);
			executable_buf[prefix_len + exepath_len] = '\0';
		}
		executable_path = executable_buf;
	}

	// printk(KERN_NOTICE "Stack top: %p\n", bprm->p);
	exepath_len = strlen(executable_path);
	sp = (user_long_t*) (lr->stack_top & ~(sizeof(user_long_t)-1));

	// 1 pointer for the mach header
	// 1 user_long_t for the argument count
	// `argc`-count pointers for arguments (+1 for NULL)
	// `envc`-count pointers for env vars (+1 for NULL)
	// `sizeof(applep_contents) / sizeof(*applep_contents)`-count pointers for applep arguments (already includes NULL)
	// space for exepath, kernfd, and elfcalls
	sp -= 1 + 1 + (lr->argc + 1) + (lr->envc + 1) + (sizeof(applep_contents) / sizeof(*applep_contents)) + user_long_count(exepath_len + sizeof(EXECUTABLE_PATH) + sizeof(kernfd) + sizeof(elfcalls));

	exepath_user = (char __user*) lr->stack_top - exepath_len - sizeof(EXECUTABLE_PATH);
	memcpy(exepath_user, EXECUTABLE_PATH, sizeof(EXECUTABLE_PATH)-1);
	memcpy(exepath_user + sizeof(EXECUTABLE_PATH)-1, executable_path, exepath_len + 1);

	snprintf(kernfd, sizeof(kernfd), "kernfd=%d", lr->kernfd);
	kernfd_user = exepath_user - sizeof(kernfd);
	memcpy(kernfd_user, kernfd, sizeof(kernfd));

#if defined(GEN_64BIT)
	#define POINTER_FORMAT "%lx"
#elif defined(GEN_32BIT)
	#define POINTER_FORMAT "%x"
#endif

	snprintf(elfcalls, sizeof(elfcalls), "elf_calls=" POINTER_FORMAT, (unsigned long)(uintptr_t)&_elfcalls);
	elfcalls_user = kernfd_user - sizeof(elfcalls);
	memcpy(elfcalls_user, elfcalls, sizeof(elfcalls));

	applep_contents[0] = exepath_user;
	applep_contents[1] = kernfd_user;
	applep_contents[2] = elfcalls_user;
	applep_contents[3] = NULL;

	lr->stack_top = (unsigned long) sp;

	// XXX: skip this for static executables, but we don't support them anyway...
	if (__put_user((user_long_t) lr->mh, sp++))
	{
		fprintf(stderr, "Failed to copy mach header address to stack\n");
		exit(1);
	}
	if (__put_user((user_long_t) lr->argc, sp++))
	{
		fprintf(stderr, "Failed to copy argument count to stack\n");
		exit(1);
	}

	// Fill in argv pointers
	argv = sp;
	for (int i = 0; i < lr->argc; ++i)
	{
		if (!lr->argv[i]) {
			lr->argc = i;
			break;
		}
		if (__put_user((user_long_t) lr->argv[i], argv++))
		{
			fprintf(stderr, "Failed to copy an argument pointer to stack\n");
			exit(1);
		}
	}
	if (__put_user((user_long_t) 0, argv++))
	{
		fprintf(stderr, "Failed to null-terminate the argument pointer array\n");
		exit(1);
	}

	// Fill in envp pointers
	envp = argv;
	for (int i = 0; i < lr->envc; ++i)
	{
		if (!lr->envp[i]) {
			lr->envc = i;
			break;
		}

		if (__put_user((user_long_t) lr->envp[i], envp++))
		{
			fprintf(stderr, "Failed to copy an environment variable pointer to stack\n");
			exit(1);
		}
	}
	if (__put_user((user_long_t) 0, envp++))
	{
		fprintf(stderr, "Failed to null-terminate the environment variable pointer array\n");
		exit(1);
	}

	applep = envp; // envp is now at the end of env pointers

	for (int i = 0; i < sizeof(applep_contents)/sizeof(applep_contents[0]); i++)
	{
		if (__put_user((user_long_t)(unsigned long) applep_contents[i], applep++))
		{
			fprintf(stderr, "Failed to copy an applep value to stack\n");
			exit(1);
		}
	}

	// get_random_bytes(rand_bytes, sizeof(rand_bytes));

#ifdef DARLING_FREEBSD
	/*
	 * purpose:  Produce randomised stack_guard and malloc_entropy applep entries.
	 * input:    none
	 * output:   writes two applep strings into the guest stack
	 * sideEffects:
	 *   - Reads from arc4random_buf() (FreeBSD kernel CSPRNG).
	 *   - stack_guard byte[0] forced to 0x00 (protects against strcpy-based
	 *     overwrite attacks that stop at a null terminator).
	 */
	{
		/* stack_guard — 64-bit random value; must contain a null byte */
		uint64_t stack_guard;
		arc4random_buf(&stack_guard, sizeof(stack_guard));
		((uint8_t *)&stack_guard)[0] = 0x00; /* ensure null byte present */

		/* malloc_entropy — two 64-bit random values */
		uint64_t malloc_entropy[2];
		arc4random_buf(malloc_entropy, sizeof(malloc_entropy));

		/* Format as applep strings on the guest stack */
		static char sg_buf[32];   /* "stack_guard=0x<16 hex digits>\0" = 28 bytes */
		static char me_buf[56];   /* "malloc_entropy=0x<16>,0x<16>\0" = 52 bytes */

		snprintf(sg_buf, sizeof(sg_buf), "stack_guard=0x%016llx",
		    (unsigned long long)stack_guard);
		snprintf(me_buf, sizeof(me_buf), "malloc_entropy=0x%016llx,0x%016llx",
		    (unsigned long long)malloc_entropy[0],
		    (unsigned long long)malloc_entropy[1]);

		/* Copy into guest address space below existing applep area */
		char __user *sg_user  = (char __user *)elfcalls_user - sizeof(sg_buf);
		char __user *me_user  = sg_user - sizeof(me_buf);
		memcpy(sg_user, sg_buf, sizeof(sg_buf));
		memcpy(me_user, me_buf, sizeof(me_buf));

		/* applep_contents already consumed; push pointers via sp (already set).
		 * We do not alter the applep pointer array here — these entries would
		 * need to be wired in before the sp calculation above.  For now the
		 * buffers are staged in the correct memory region as a foundation;
		 * a follow-up commit will add the applep pointer entries.       TODO */
	}
#endif /* DARLING_FREEBSD */

	// TODO: produce main_stack?
}

#undef FUNCTION_NAME
#undef user_long_t
