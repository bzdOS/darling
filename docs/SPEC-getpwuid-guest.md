# SPEC: why `getpwuid()` finds nobody in the guest

## Status: root cause found by static reading — a real/virtual UID split, not a
missing-env-var or a lazy-init race. Confidence is high on the code chain, but the
final link ("what real FreeBSD uid does the guest process actually run as") was not
directly observed (read-only task, no ktrace/live trace). See §6.

## 0. tl;dr

`getpwuid()` never touches `/etc/passwd` for `uid==0` — by design, it goes to
`/etc/master.passwd` instead (`libinfo.c:251`). That file is mode `0600` root:root
in the overlay. The Darwin-level "we're root" (`geteuid()==0`) is a **purely virtual**
number stored server-side (`task->xnu_task.audit_token.val[1]`, zero-initialized —
`task.c:78`) and is completely decoupled from the real FreeBSD credentials of the
process that actually executes the `openat()` syscall. If that real process is not
real-root, `fopen(_PATH_MASTERPASSWD)` fails with `EACCES`, `_fsi_get_user()` returns
`NULL` (`file_module.c:860`), `getpwuid()` returns `NULL`
(`libinfo.c:251-257`), and `CFCopyHomeDirectoryURLForUser(NULL)` — which calls
`getpwuid()` directly and has **no `$HOME` fallback at all** — returns `NULL`
(`CFPlatform.c:346-350`). That's the same failure `docs/SPEC-cfpreferences-domain-list.md`
traced down to an empty preferences directory. `$HOME` being absent from the
environment is a symptom of the same misconfiguration, not a cause: this code path
never reads `$HOME`.

## 1. `si_search_file()` / `si_module_static_file` — cannot return NULL here

`si_search_file()` (`libinfo.c:84-90`) calls `si_module_with_name("file")`
(`si_module.c:178`). `"file"` is in the unconditional static `modules[]` table at
`si_module.c:190` (no `#ifdef` guard, unlike `muser`/`ds`), so the name lookup at
`si_module.c:216-227` always matches on the first loop iteration — it never falls
through to the dynamic-bundle-loading path (`si_module.c:232+`) that could fail with
`ENOMEM`/`ECONNREFUSED`.

Its `init()` is `si_module_static_file()` (`file_module.c:2417-2504`). The
`dispatch_once` body (`file_module.c:2486-2499`) only does `strdup("file")` and
`calloc(sizeof(file_si_private_t))`, sets a hardcoded `validation_notify_mask`, and
returns. **No filesystem access, no vchroot call, nothing that can fail** except
`strdup`/`calloc` returning `NULL` on OOM (checked at `file_module.c:2492`, and even
if `pp` is `NULL` the module object itself is still returned non-NULL). So:
**hypothesis 4 (lazy-init raced ahead of vchroot readiness) is refuted** — the file
module's own init has no vchroot dependency to race against. (`si_module_static_search()`,
the other module used as a fallback, is the same shape: `search_module.c:930-999`,
also a trivial `dispatch_once` with no I/O in the init body itself — only
`SYSINFO_CONF_ENABLE`-gated, off by default, `search_module.c:983-989`.)

## 2. The file module opens `/etc/master.passwd`, not `/etc/passwd`, for uid 0

`getpwuid()` (`libinfo.c:245-262`):

```c
// Search the file module first for all system uids
// (ie, uid value < 500) since they should all be
// in the /etc/*passwd file.
if (uid < SYSTEM_UID_LIMIT)          // SYSTEM_UID_LIMIT == 500, libinfo.c:63
    item = si_user_byuid(si_search_file(), uid);
if (item == NULL)
    item = si_user_byuid(si_search(), uid);   // fallback, see §2a
```

`file_user_byuid()` → `_fsi_get_user()` (`file_module.c:2129-2131`, `830-893`):

```c
if (geteuid() == 0)
{
    f = fopen(_PATH_MASTERPASSWD, "r");   // file_module.c:850  ("/etc/master.passwd", pwd.h:55)
    ...
}
else
{
    f = fopen(_PATH_PASSWD, "r");         // file_module.c:855  ("/etc/passwd")
    ...
}
if (f == NULL) return NULL;               // file_module.c:860 — silent, no error surfaced
```

Since we run as (virtual) uid 0, **this branch always takes `_PATH_MASTERPASSWD`**.
This is exactly consistent with the measured trace never opening `/etc/passwd` — it
was never supposed to for this uid. It is *not* evidence that the lookup succeeded.
`_PATH_MASTERPASSWD` is `/etc/master.passwd` (`pwd.h:55`).

Verified independently: both files exist in the overlay with correctly-shaped content
for the two parsers (`_fsi_parse_user`, `file_module.c:779-828`, which requires
exactly 10 `:`-fields for `master.passwd` format and 7 for `passwd` format):

```
$ awk -F: '{print NF}' artefacts/darling-overlay/etc/master.passwd   # → 10, 10, 10 (root/nobody/testuser)
$ awk -F: '{print NF}' artefacts/darling-overlay/etc/passwd          # → 7, 7, 7
```

So content/format is not the problem. Permissions are the difference that matters:

```
$ stat artefacts/darling-overlay/etc/master.passwd
Access: (0600/-rw-------)  Uid: (0/root)  Gid: (0/root)
$ stat artefacts/darling-overlay/etc/passwd
-rw-r--r--                 Uid: (0/root)  Gid: (0/root)
```

(`artefacts/darling-overlay/etc` is itself a symlink to `private/etc`, ordinary
Apple-layout indirection — `readlink` shows `private/etc`; not itself suspicious.)

### 2a. The fallback (`si_search()`) does not help

`si_search()` → `"search"` module's default chain is `cache, file, mdns`
(`search_module.c:952-966`, `DARLING` branch uses `darling-resolver` in place of
`mdns` per `search_module.c:963-964`). It includes `"file"` again, so on cache-miss
it re-enters the exact same `_fsi_get_user()` code path and fails the same way. There
is no second, independently-privileged code path here — `getpwuid()` has exactly one
real data source in this build (barring `ds`/`muser`, both `#ifdef`-disabled per the
modules table).

## 3. `vchroot_expand()` — traced, not the breaking link

`sys_openat_nocancel()` (`.../fcntl/openat.c:36-83`) calls `vchroot_expand()`
(`.../linux_premigration/vchroot_userspace.c:169-224`) before issuing the real
`openat` syscall. For an absolute path (`/etc/master.passwd` qualifies,
`vchroot_userspace.c:196` branch not taken), it prepends `prefix_path` — fetched once
via `dserver_rpc_vchroot_path()` (`vchroot_userspace.c:154-166`, guarded by
`prefix_path_len == -1`, `vchroot_userspace.c:174-175`) — to the requested path and
walks components (`vchroot_run()`, not fully re-read here since §2/§4 below make it
moot: **this mechanism is generic and path-agnostic**; it is exercised successfully
by every other `/etc/*` read this system already depends on (the `defaults`
CoreFoundation trace in `SPEC-cfpreferences-domain-list.md` shows the same
`__darling_bsd_syscall` → Linux-syscall-trap → `vchroot`-resolved path route working
for directory listings). There is nothing in `vchroot_expand()` that special-cases a
`0600` file or a `master.passwd` name. **Ruled out as the guest-visible cause of a
resolution failure** — if `vchroot_expand()` mis-resolved the path, the failure mode
would be `ENOENT`/wrong-tree, not a permissions-shaped silent NULL, and it would also
break every other file read, which — per the CF trace already analyzed — isn't
happening.

## 4. The real bug: the virtual UID is fake, and the file's permissions are real

`geteuid()` (`.../unistd/geteuid.c:9-15`) calls `__getuidgid()`
(`.../unistd/getuid.c:20-56`), which — on first call — does one RPC,
`dserver_rpc_uidgid(-1, -1, &stored_uid, &stored_gid)` (`getuid.c:41`), and caches
the result process-wide (`stored_uid`/`stored_gid`, `getuid.c:17-18`, protected by
`uidgid_rwlock`). Server-side, this lands in
`DarlingServer::Call::Uidgid::processCall()` (`darlingserver/src/call.cpp:533-551`),
which calls `dtape_task_uidgid(process->_dtapeTask, -1, -1, &uid, &gid)`
(`call.cpp:542`) — with `new_uid == -1` this is a pure read, no write
(`duct-tape/src/task.c:170-187`, the `if (new_uid >= 0)` guard at line 178 skips the
write branch).

What it reads is `task->xnu_task.audit_token.val[1]` / `val[2]`
(`task.c:173,176`) — fields of the **emulated XNU task struct**, which is
`memset(&task->xnu_task, 0, sizeof(task->xnu_task))`-zeroed at task creation
(`duct-tape/src/task.c:78`, inside `dtape_task_create()`, `task.c:48+`) and **never
written afterward** unless some code explicitly calls the `uidgid` RPC with a
non-negative `new_uid`/`new_gid` (e.g. an emulated `setuid()`/`seteuid()`, which
nothing in a bare `defaults read` invocation does). No code path found anywhere in
`darlingserver/src/process.cpp` or `duct-tape/src/task.c` that seeds this field from
the *real* credentials of the process performing the RPC — it is `grep`-confirmed
absent (`process.cpp` has zero `audit_token`/`uid` hits besides an unrelated comment
at line 323).

**Conclusion: every guest task's Darwin-visible `geteuid()` is unconditionally `0`
by construction — always, for every process, regardless of who actually launched
it.** This is a deliberate simplification (there's no login/authentication flow in
this environment to seed a "real" per-user identity), but it means the branch at
`libinfo.c:251` (`if (uid < SYSTEM_UID_LIMIT)`, and implicitly the
`geteuid()==0` check at `file_module.c:848`) is **decided by a number that has no
relationship to the real FreeBSD-level identity of the process making the actual
`openat()` syscall.** That real syscall is subject to the real kernel's real
permission check against the real file's real owner/mode (`0600 root:root`). If the
real process is not real-root, `open()` returns `EACCES`, `errno_linux_to_bsd()`
maps it straight through (no special-casing for this errno visible in the parts of
that conversion file touched by this trace), `fopen()` returns `NULL`
(`file_module.c:850`), and the whole chain in §2 collapses silently — no crash, no
stderr, exactly the observed symptom.

This directly explains why hypothesis 5 in the task brief is a dead end too: **the
file module never calls `getenv()` at all** (`grep getenv file_module.c` — zero
hits), and the actual CF call this bug surfaces through,
`CFCopyHomeDirectoryURLForUser(NULL)` (`CFPlatform.c:344-350`), calls
`getpwuid(euid ? euid : getuid())` directly with **no `$HOME`/`$USER`/`$LOGNAME`
fallback in this function at all**. Setting `HOME` in the harness's environment
would not change a single byte of the code path that's actually failing here — it's
a real, separate finding from the missing-`HOME` symptom already noted in
`SPEC-cfpreferences-domain-list.md`, not a fix for it.

## 5. Ranked fix candidates

1. **(Cheapest, no code change) Loosen `/etc/master.passwd` permissions in the
   overlay to at least world-readable (e.g. `0644`), matching `/etc/passwd`.**
   Since the virtual-uid gate at `file_module.c:848` is already fake (§4) — it does
   not correspond to any real access-control boundary in this environment, only to
   which of two files gets opened — the real-file permission bit is not protecting
   anything today; it is only breaking the emulated-root lookup path. This is a
   one-line overlay/image-build change (`chmod`), no rebuild of any binary, testable
   immediately. **Recommended as the immediate fix.** Residual risk: this is a
   fidelity trade-off (`master.passwd` really is `0600`-only on real macOS) — fine
   for a dev/emulation sandbox where the uid model is already virtual, worth a
   one-line comment explaining why.

2. **(Verify, possibly free) Confirm what real FreeBSD uid actually launches the
   guest `mldr`/Darling process, and run it as real root if it currently isn't.**
   Per project conventions (`CLAUDE.md`), interactive access to the dev VM is via the
   unprivileged `freebsd` SSH user, with root reached only via `su -m root` for
   specific privileged operations (jail mgmt, etc.) — it was **not verified in this
   task** (no launcher script for the Darling/mldr guest process was found under
   `infra/scripts/` or `infra/systemd/` in this pass) whether the actual `defaults`/CF
   test invocation runs as real root or as `freebsd`. If it's the latter, that alone
   is sufficient to reproduce this bug even with option 1 not applied to *other*
   root-only files elsewhere in `/etc` that the same virtual-root assumption governs
   (e.g. `_PATH_MASTERPASSWD`-reading tools other than `getpwuid` — `_fsi_get_user`
   is the only caller found, but the same virtual/real split affects anything else
   gated the same way). Fixing the launcher to actually run as real root is more
   "correct" than option 1 but is an infra/deploy change outside this repo's source
   tree, and was not located in this pass — flagged as unverified, see §6.

3. **(Root-cause-correct, bigger) Seed `task->xnu_task.audit_token.val[1]`/`[2]`
   from the real credentials of the process that registers with darlingserver,
   instead of leaving it zero-initialized (`task.c:78`).** This makes the emulated
   `geteuid()` agree with the real kernel's permission checks by construction, for
   every file access, not just `master.passwd` — the structurally correct fix. Cost:
   touches `darlingserver`/`duct-tape` C++ (`dtape_task_create()`, plus whatever
   currently calls it — `call.cpp` and the process-registration RPC handler), needs a
   real getuid() of the peer process (e.g. via `SO_PEERCRED`/similar on the
   registration socket, not verified whether that plumbing already exists), and a
   full rebuild + retest. Out of scope for a same-day fix; worth doing once the
   sandbox model matures past "everything is uid 0."

4. **(Not recommended) Patch mldr to special-case `/etc/master.passwd` or bypass the
   real permission check for known system files.** Rejected: this fakes the
   permission model further in a different, more hidden place (inside the syscall
   trap dispatcher) rather than fixing the actual inconsistency, and would silently
   diverge from real FreeBSD semantics for one hardcoded path — the kind of
   "workaround substituted for the named fix" this project's own conventions flag
   against.

**Recommendation: do #1 now** (it's a data file, not a build artifact, changeable
without any compile step and immediately testable), **and open #2 as a follow-up
question** for whoever owns the guest-process launcher, since if the answer to #2 is
"it already runs as real root," #1 is not even needed and the actual bug is
somewhere not found in this pass. Track #3 as the real structural fix once the
sandbox's identity model is revisited.

## 6. What was NOT checked (explicitly unverified)

- **No live trace, no build, no VM interaction** — per task constraints, everything
  above is static source reading plus one filesystem `stat`/`awk` check of the
  overlay's `/etc/passwd` and `/etc/master.passwd` (read-only, no files edited).
- **The real FreeBSD uid of the process that actually runs the guest
  (`mldr`/Darling binary) when `defaults read`/CF is invoked was not determined.**
  No launcher script referencing `darlingserver` or `mldr` was found under
  `/path/to/infra/scripts/` or `/path/to/infra/systemd/` in this pass (grepped,
  zero hits) — it may live in a script or systemd/rc.d unit not covered by those two
  directories, or be started interactively. This is the single fact that would
  confirm or refute the "real launcher runs unprivileged" half of §4/§5's
  recommendation #2, and it needs whoever runs the harness (or a live
  `ps`/`procstat` on the guest) to answer, not static reading.
- **`errno_linux_to_bsd()` was not read** (flagged as unchecked in
  `SPEC-cfpreferences-domain-list.md` too) — did not confirm that an `EACCES` from
  the real `openat()` actually survives translation as `EACCES` rather than being
  remapped to something `fopen()`/`_fsi_get_user()` would treat differently. Assumed
  standard passthrough; not verified byte-for-byte.
- **`vchroot_run()`'s component-walk body was not fully re-read line by line** in
  this pass (only its entry point and the absolute-path branch) — relying on the
  prior spec's finding that the same mechanism works for other `/etc` reads. If
  `/etc/master.passwd` specifically hit some symlink-depth or case-folding edge case
  distinct from ordinary files, that was not independently re-verified here.
- **Whether anything upstream of `defaults`/CF ever calls the emulated
  `setuid()`/`seteuid()` RPC (`dtape_task_uidgid` with `new_uid >= 0`) before this
  lookup happens** — only confirmed the field starts zeroed and found no caller in
  the reviewed files; a full repo-wide search for every `dserver_rpc_uidgid(` call
  site outside `getuid.c`/`call.cpp` was not performed.
- **Did not check the i386 slice** — x86_64 only, consistent with the prior spec's
  scope and this project's primary dev-loop target.
- **Did not verify `SO_PEERCRED`-equivalent plumbing exists or not** for fix
  candidate #3 — that recommendation's cost estimate is a guess based on not finding
  it in the files read, not a confirmed absence.
