место краша = DarlingServer::Process::_dispose (process.cpp:1011), `this == nullptr`

Date: 2026-11-26 01:30-01:45 UTC. Branch base: local trunk `2c35efbf12472bef03f12d6cf6e44a25793c2aee`
(step 12). No fix in this commit: the order is verdict of the place first, then a fix branch.
Measurements below are from two runs of the same failing pair, one under lldb, one bare.

## 1. The place, from the backtrace (lldb, batch, `bt 25`)

Target `$DARLING_BUILD_DIR/dserver/dserver-build/darlingserver`, args `<run-prefix> 1001 1001 3 0`,
env `DARLING_VCHROOT_PATH=/tmp/darling-local-overlay`, `DARLING_MLDR_PATH=$DARLING_BUILD_DIR/dserver/mldr-real/mldr`,
`DSERVER_LOG_LEVEL=debug`, `DSERVER_LOG_STDERR=1`. Stop: `SIGSEGV: address not mapped to object
(fault address: 0x160)`. Frame #0 verbatim:

```
frame #0: 0x000000000031c0eb darlingserver`std::__1::enable_if<is_move_constructible<DarlingServer::Process*>::value && is_move_assignable<DarlingServer::Process*>::value, void>::type std::__1::swap[abi:se190107]<DarlingServer::Process*>(__x=0x0000000820afc340, __y=0x0000000000000160) at swap.h:43:9
```

`bt 25` (frames #0-#11, the whole stack — the process is 11 frames deep, there is nothing below `_start`):

```
* thread #1, name = 'darlingserver', stop reason = signal SIGSEGV: address not mapped to object (fault address: 0x160)
  * frame #0: 0x000000000031c0eb ... std::__1::swap[abi:se190107]<DarlingServer::Process*>(__x=0x0000000820afc340, __y=0x0000000000000160) at swap.h:43:9
    frame #1: 0x000000000031c0b1 ... std::__1::shared_ptr<DarlingServer::Process>::swap[abi:se190107](this=0x0000000820afc340, __r=nullptr) at shared_ptr.h:714:5
    frame #2: 0x0000000000315372 ... std::__1::shared_ptr<DarlingServer::Process>::operator=[abi:se190107](this=0x0000000000000160, __r=nullptr) at shared_ptr.h:683:32
    frame #3: 0x0000000000313375 ... DarlingServer::Process::_dispose(this=0x0000000000000000) at process.cpp:1011:17
    frame #4: 0x00000000002f68d5 ... DTapeHooks::dtape_hook_task_context_dispose(task_context=0x0000000000000000) at server.cpp:518:55
    frame #5: 0x00000000003fa4c8 ... dtape_task_create(parent_task=0x0000000000000000, nsid=0, context=0x0000000000000000, architecture=dserver_rpc_architecture_x86_64) at task.c:75:22
    frame #6: 0x00000000003fa397 ... dtape_task_init at task.c:43:7
    frame #7: 0x00000000003efb0b ... dtape_init(hooks=0x000000000021d710) at init.c:81:2
    frame #8: 0x00000000002ef5b9 ... DarlingServer::Server::start(this=0x00001e658fc67000) at server.cpp:723:2
    frame #9: 0x00000000002e2fb2 ... main(argc=6, argv=0x0000000820afead8) at darlingserver.cpp:817:10
    frame #10: 0x0000000826c5e23f libc.so.7`__libc_start1(...) at libc_start1.c:180:7
    frame #11: 0x00000000002df074 ... _start at crt1_s.S:80
```

`frame info` for #1 and #2 (the two frames the order asked for, with the source line lldb resolved):

```
frame #1: shared_ptr<DarlingServer::Process>::swap(this=0x0000000820afc340, __r=nullptr) at shared_ptr.h:714:5
-> 714      std::swap(__ptr_, __r.__ptr_);
frame #2: shared_ptr<DarlingServer::Process>::operator=(this=0x0000000000000160, __r=nullptr) at shared_ptr.h:683:32
-> 683      shared_ptr(std::move(__r)).swap(*this);
```

Registers at the stop (`register read rdi rsi rbp rsp`): `rdi=0x0000000820afc340`, `rsi=0x0000000000000160`,
`rbp=0x0000000820afc2f0`, `rsp=0x0000000820afc2f0`. `image lookup -a 0x31c0eb`:

```
Address: darlingserver[0x000000000031c0eb] (darlingserver.PT_LOAD[1]..text + 250011)
Summary: darlingserver`... std::__1::swap[abi:se190107]<DarlingServer::Process*>(DarlingServer::Process*&, DarlingServer::Process*&) + 27 at swap.h:43:9
```

## 2. Why 0x160: a null `this` plus the member offset

`_dispose` is one line, `process.cpp:1011`:

```
void DarlingServer::Process::_dispose() {
	_selfReference = nullptr;
};
```

`_selfReference` is `std::shared_ptr<Process>` (process.hpp:108), i.e. two pointers; its first pointer
sits at offset `0x160` inside `Process`. Frame #3 has `this=0x0000000000000000`, so the assignment
addresses `0x0 + 0x160 = 0x160` — exactly the fault address, and exactly the `__y=0x160` lldb shows in
frame #0 (`swap(__ptr_, __r.__ptr_)` takes the address of the member's first pointer). The fault is a
write to a near-null address, not a corrupted pointer: the register `rsi` holds `0x160` and `rdi` holds
the `shared_ptr` temporary at `0x820afc340`.

The null `this` comes from the hook that calls it, `server.cpp:518`:

```
static void dtape_hook_task_context_dispose(void* task_context) {
	static_cast<DarlingServer::Process*>(task_context)->_dispose();
};
```

No null check — while the hook immediately below it in the same file does check:

```
static dtape_eternal_id_t dtape_hook_task_eternal_id(void* task_context) {
	if (!task_context) {
		return DarlingServer::EternalIDInvalid;
	}
```

The caller passes `context=0x0` explicitly: `dtape_task_init` (task.c:43) calls
`dtape_task_create(NULL, 0, NULL, arch)` — the kernel task is created with a NULL context by design.
The dispose hook is reached on that path through `task_deallocate` (task.c:233) -> `dtape_task_destroy`
(task.c:165) -> `dtape_hooks->task_context_dispose(task->context)`. Frame #5's `task.c:75:22` is the
inlined line table of that call (the file's `task_context_dispose` call site is line 165; the binary was
built optimized, so the line attribution inside `dtape_task_create` is approximate — the symbol and the
argument `context=0x0` are not).

So the crash is in dserver's own start, inside `dtape_init`, before any guest RPC: the kernel task's
context is NULL, and the dispose hook dereferences it without a check.

## 3. dserver.log tail (the server's own log, fd 4 in the step-12 trace)

Debug-level run (the lldb run above), kept prefix, `private/var/log/dserver.log`, 532 bytes, complete:

```
[1795656611.742568](dtape, Debug) -1(-1):-1(-1): dtape_processor_init
[1795656611.742637](dtape, Debug) -1(-1):-1(-1): dtape_memory_init
[1795656611.742660](dtape, Debug) -1(-1):-1(-1): dtape_timer_init
[1795656611.742665](dtape, Debug) -1(-1):-1(-1): dtape_mk_timer_init
[1795656611.742669](dtape, Debug) -1(-1):-1(-1): timer_call_init
[1795656611.742677](dtape, Debug) -1(-1):-1(-1): ipc_table_init
[1795656611.742685](dtape, Debug) -1(-1):-1(-1): ipc_voucher_init
[1795656611.742690](dtape, Debug) -1(-1):-1(-1): dtape_task_init
```

The last line is `dtape_task_init` — the log stops exactly where the backtrace puts the crash
(`dtape_task_init` -> `dtape_task_create`), and nothing about the socket, the bind or a guest appears in it.

Bare run, no debugger, default log level, kept prefix: exit by signal 11 (rc 139), `dserver.log` 0 bytes —
empty. That is a measurement, not a lost log: at the default level the server writes nothing before the
crash, so the own-log evidence exists only at debug level.

## 4. PROVENANCE of the runs quoted here

From the `PROVENANCE` lines of the step-13 batch logs (`chrome-sr-st13s11.log`, `chrome-sr-st13s21.log`):

```
PROVENANCE git_head=2c35efbf12472bef03f12d6cf6e44a25793c2aee overlay_appkitextras=450ece775950b0a8dc38741b03d897b5
PROVENANCE mldr_installed=1ff2ed031367d6ee869a45402c99dba5 darlingserver_installed=df9b743a8c3d0a9d31906b2292904f41
PROVENANCE mldr_build=bb8571303d6a4598b08a76fff1ca542d darlingserver_build=df9b743a8c3d0a9d31906b2292904f41   (st13s11, working loader)
PROVENANCE mldr_build=2f5976f510f94a1731a49c24a9ce52e5 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41   (st13s21, trunk loader)
PROVENANCE appkit_overlay=eb1b622f6e0e348aa03ac103ff241e37 wayland_backend_overlay=f34bf90f5043b6ab6fd2cd81614d5708
PROVENANCE appkitextras_staged=450ece775950b0a8dc38741b03d897b5
```

The server binary is the same `df9b743a8c3d0a9d31906b2292904f41` in the working pin and in the failing
runs; the two lldb/direct runs above used that same binary (`md5 df9b743a8c3d0a9d31906b2292904f41`) and
the trunk loader `2f5976f510f94a1731a49c24a9ce52e5`. Reproduction of the crash alone (no guest, no
loader): `darlingserver <prefix> 1001 1001 3 0` -> signal 11 before the first RPC.

## 5. What this changes for the fix (not done here)

The crash does not depend on the guest, the loader image or the socket: it is a null-context dereference
in the dispose hook during `dtape_init`. A fix branch has to make the hook tolerate a NULL context
(the `eternal_id` hook already does) and/or keep `dtape_task_create` from disposing a context it never
set. Which of the two is the right place is the next step, after this verdict.