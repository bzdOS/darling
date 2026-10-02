#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <locale.h>
#include <unistd.h>
#include <execinfo.h> /* backtrace — exit-caller logging slice */
#include "elfcalls.h"
#include "threads.h"
#include "trap_log.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <darlingserver/rpc.h>

/* exit-caller logging slice: gate checked in normal context (these wrappers
 * never run inside a signal handler), getenv per call is fine there. */
static int mldr_trap_log_enabled_elf(void)
{
	return getenv("DARLING_TRAP_LOG") != NULL;
}

static void* dlopen_simple(const char* name)
{
	return dlopen(name, RTLD_LAZY);
}

static void* dlopen_fatal(const char* name)
{
	void* rv = dlopen_simple(name);
	if (!rv)
	{
		fprintf(stderr, "Cannot load %s (ELF): %s\n", name, dlerror());
		abort();
	}
	return rv;
}

static void* dlsym_fatal(void* handle, const char* sym)
{
	void* addr;

	/* exit-caller logging slice: the prime suspect for the (a) park —
	 * a failed dlsym on a guest-created thread must show entry, result
	 * and tid before the fatal path runs. */
	if (mldr_trap_log_enabled_elf())
		mldr_tlogx("elf-dlsym ENTER", (const void*)sym,
			   (long)(uintptr_t)handle);

	addr = dlsym(handle, sym);

	if (mldr_trap_log_enabled_elf())
		mldr_tlogx("elf-dlsym RETURN", addr, addr == NULL);

	if (!addr)
	{
		fprintf(stderr, "Failed to lookup symbol %s (ELF): %s\n", sym, dlerror());
		abort();
	}
	return addr;
}

/* exit-caller logging slice: every guest-reachable host exit(3) passes
 * through this wrapper — the caller of record for the P_WEXIT evidence
 * (TRAP-WEDGE-READ.md). backtrace/backtrace_symbols_fd run in normal
 * context here; the marker itself stays write(2)-only. */
static void elfcalls_exit(int ec)
{
	if (mldr_trap_log_enabled_elf()) {
		void *bt[8];
		int n;

		mldr_tlog("elf-exit CALL", ec, 0);
		n = backtrace(bt, 8);
		if (n > 0)
			backtrace_symbols_fd(bt, n > 4 ? 4 : n, 2);
	}
	exit(ec);
}

static int dlclose_fatal(void* handle)
{
	if (dlclose(handle) != 0)
	{
		fprintf(stderr, "Cannot dlclose library (ELF): %s\n", dlerror());
		abort();
	}
	return 0;
}

static int get_errno(void)
{
	return errno;
}

extern struct sockaddr_un __dserver_socket_address_data;

static const void* __dserver_socket_address(void) {
	return &__dserver_socket_address_data;
};

extern void __mldr_close_rpc_socket(int socket);

extern int __mldr_create_process_lifetime_pipe(int* fds);
extern void __mldr_close_process_lifetime_pipe(int fd);
extern int __dserver_process_lifetime_pipe_fd;

static int __dserver_get_process_lifetime_pipe() {
	return __dserver_process_lifetime_pipe_fd;
}

static int __dserver_process_lifetime_pipe_refresh() {
	int pipe[2];

	if (__mldr_create_process_lifetime_pipe(pipe) == -1) {
		fprintf(stderr, "Failed to create process lifetime pipe: %d (%s)\n", errno, strerror(errno));
		abort();
	}

	__dserver_process_lifetime_pipe_fd = pipe[1];
	return pipe[0];
}

void elfcalls_make(struct elf_calls* calls)
{
	calls->dlopen = dlopen_simple;
	calls->dlclose = dlclose;
	calls->dlsym = dlsym;
	calls->dlerror = dlerror;

	calls->dlopen_fatal = dlopen_fatal;
	calls->dlsym_fatal = dlsym_fatal;
	calls->dlclose_fatal = dlclose_fatal;

	calls->darling_thread_create = __darling_thread_create;
	calls->darling_thread_terminate = __darling_thread_terminate;
	calls->darling_thread_get_stack = __darling_thread_get_stack;

	calls->get_errno = get_errno;
	calls->exit = elfcalls_exit;

	calls->malloc = malloc;
	calls->free = free;
	calls->realloc = realloc;

	calls->sysconf = sysconf;

	*((void**)&calls->sem_open) = sem_open;
	*((void**)&calls->sem_wait) = sem_wait;
	*((void**)&calls->sem_trywait) = sem_trywait;
	*((void**)&calls->sem_post) = sem_post;
	*((void**)&calls->sem_close) = sem_close;
	*((void**)&calls->sem_unlink) = sem_unlink;

	*((void**)&calls->shm_open) = shm_open;
	*((void**)&calls->shm_unlink) = shm_unlink;

	calls->dserver_socket_address = __dserver_socket_address;
	calls->dserver_per_thread_socket = __darling_thread_rpc_socket;
	calls->dserver_per_thread_socket_refresh = __darling_thread_rpc_socket_refresh;
	calls->dserver_close_socket = __mldr_close_rpc_socket;

	calls->dserver_get_process_lifetime_pipe = __dserver_get_process_lifetime_pipe;
	calls->dserver_process_lifetime_pipe_refresh = __dserver_process_lifetime_pipe_refresh;
	calls->dserver_close_process_lifetime_pipe = __mldr_close_process_lifetime_pipe;
}
