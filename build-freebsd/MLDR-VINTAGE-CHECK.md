# MLDR-VINTAGE-CHECK — does an older source vintage remove -61

## Verdict (one line)

**No**: the nearest buildable vintage (`8fa5ecf2a`, 2026-06-23) reports `-61`
in **2 of 2** runs and does reach checkin, so the older source does not remove
the defect — and the only row without `-61` (the known-good pin) is a run that
never reaches checkin at all, so its zero is the absence of the event.

## 1. The named vintage does not build

Named: `0f6878684^` = `853c98ccebf9ddc289fe7a3fada82479c4ac64bc`
(2026-06-23, `fix(darlingserver): bump submodule`).

Built it four times, each attempt failing later than the previous one, always
outside git (export via `git archive`, tree untouched):

```
attempt 1  src/startup/mldr/include/sys/_types.h in that vintage is an empty
           include guard, so it shadows the system header and the compiler
           loses the types:
           /usr/include/sys/_iovec.h:38:9: error: unknown type name '__size_t'
attempt 2  after dropping that empty guard (the pass-through arrived later, in
           the "mldr compat headers" fix):
           resources/dserver-rpc-defs.h:13:10: fatal error: 'rtsig.h' file not
           found
attempt 3  after generating rtsig.h the way the era's scripts did (65/126,
           measured on this host as SIGRTMIN=65 SIGRTMAX=126):
           generated-rpc/src/rpc.c:106:11: error: use of undeclared identifier
           'SYS_gettid'
           generated-rpc/src/rpc.c:172:102: error: use of undeclared identifier
           'ECOMM'
attempt 4  after defining the two identifiers the way the port eventually did
           (-DSYS_gettid=SYS_getpid -DECOMM=ECONNRESET):
           elfcalls/elfcalls.c:59:27: error: redeclaration of
           '__dserver_socket_address_data' with a different type: 'struct
           sockaddr_un[]' vs 'struct sockaddr_un'
           mldr.c:356:22: error: use of undeclared identifier 'CPU_TYPE_ANY'
```

Root cause, measured: that vintage has **zero** `DARLING_FREEBSD` conditionals
in `resources/dserver-rpc-defs.h`
(`grep -c DARLING_FREEBSD` → vintage `0`, trunk `2`), and its hooks are the
Linux ones:

```c
#define dserver_rpc_hooks_get_tid() ((pid_t)syscall(SYS_gettid))
#define dserver_rpc_hooks_get_communication_error_status() (-ECOMM)
```

`grep -c SYS_gettid /usr/include/sys/syscall.h` → `0`: the host has no such
syscall. The FreeBSD branches (`__freebsd_gettid`, the `ECOMM` mapping, the
header pass-through) all land in one commit — `8fa5ecf2a`
"fix(freebsd): mldr compat headers + threads.c main-thread detection",
2026-06-23 19:36 — which is **after** `0f6878684` (16:52). So no source older
than `8fa5ecf2a` builds for this host, and `853c98cce` produces no binary and
therefore no `-61` measurement of its own.

## 2. What was built instead

`8fa5ecf2a251c4838b495df432e20149ea65ba8c` (2026-06-23 19:36) — the first
vintage that compiles here, still **before** the rlimit stack sizing
(`8d8f5f2bf`, 2026-09-29), so it sizes the stack the old 16-page way like the
pin, while already carrying the WSL1 `MAP_FIXED` fix.

Build: `git archive <vintage> src/startup/mldr` into a scratch directory, the
recipe of `build-mldr-only.sh` unchanged (same `-std=gnu11 -ggdb -O2`, same
defines, same include order), everything outside `src/startup/mldr` symlinked to
the trunk tree, no worktree, no clone, no permanent directory, trunk left at
its tip.

Deviations from a pure `git archive`, all of them build-shims and none of them
checkin/stack logic:

- the empty `include/sys/_types.h` guard is dropped where the vintage has no
  pass-through (it shadows the system header);
- broken SDK symlinks that the trunk has replaced by a real file take the
  trunk's file (the 9-level-up links the recipe's own cctools comment is about);
- `rtsig.h` is generated in the scratch (65/126 — host values, matching the
  era's `printf '#define LINUX_SIGRTMIN %d\n...' 65 126 > rtsig.h`);
- `-D_BSD_I386__TYPES_H_`, the same suppression the full build of that era used;
- the host compat header is **not** force-included: it does `typedef long long
  __int64_t` and collides with the system header
  (`/usr/include/sys/_types.h:48:16: error: typedef redefinition with different
  types ('long' vs 'long long')`);
- the current patch series is not applied to the vintage sources.

Result: `md5 979075c6351ece4c71126926798a7f06`, `size 510640`,
`BUILD_RC=0`.

## 3. The two runs

Same harness as the matrix (one build-dir copy, the same `darlingserver` in
every row), each run logging its own PROVENANCE:

```
v8a  PROVENANCE git_head=8fa5ecf2a251c4838b495df432e20149ea65ba8c
v8a  PROVENANCE mldr_build=979075c6351ece4c71126926798a7f06 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
v8a  grep -c -- '-61' -> 2   grep -c checkin -> 4   $DARLING_BUILD_DIR/chrome-sr-b3v8a.log
v8b  PROVENANCE git_head=8fa5ecf2a251c4838b495df432e20149ea65ba8c
v8b  PROVENANCE mldr_build=979075c6351ece4c71126926798a7f06 darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
v8b  grep -c -- '-61' -> 2   grep -c checkin -> 4   $DARLING_BUILD_DIR/chrome-sr-b3v8b.log
```

## 4. Sections against the pin

```
                     pin bb857130…    vintage 979075c6…    delta
file size                530784            510640        -20144
.text                     89840             89504          -336
.rodata                    5216              5184           -32
.debug_info              164636            164437          -199
.debug_line               56160             54587         -1573
```

The vintage build lands within 0.4 % of the pin's `.text`, which is what a
near-identical source vintage looks like — the pin and this build differ by
days of source, not by build inputs (that was the previous step's result).

## 5. Reading

- `-61` **stays** on the older source: 2/2, and those runs do attempt checkin
  (4 checkin lines each).
- The pin's row stays at 0 `-61` with **0 checkin lines**: its guest dies at the
  64 KiB stack mapping first (`GUEST-STACK-ESRCH.md` §1), which is exactly what
  the WSL1 `MAP_FIXED` strip does and what `0f6878684` fixed. The fix is what
  lets a guest get far enough to report `-61` at all.
- So within everything that builds, `-61` is present from `8fa5ecf2a` to the
  trunk: the cause is **at or before** `8fa5ecf2a`, not in the later range.

## 6. Not measured

- `-61` on `853c98cce` and anything older: no binary exists (§1), and the next
  step older than `8fa5ecf2a` is the first step that does not build.
- Whether `-61` also appears with a `darlingserver` other than
  `df9b743a8c3d0a9d31906b2292904f41` — held fixed in every row, including this
  one.
- Whether the checkin failure depends on the guest reaching it *through the
  thread path* rather than the spawn path — both calls are in the vintage
  sources and neither was isolated.
