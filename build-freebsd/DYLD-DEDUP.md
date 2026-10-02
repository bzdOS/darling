# dyld-dedup: rebuild without the duplicates (wall #6, path 3)

Wall #6. Path 2 (muldefs) is closed — ld64.lld has no
multiple-definition switch. Path 3: remove from the worktree `glue.c`
only the fallbacks that duplicate the static libraries, then link.

## Step 0 — an env that prints the objc registration?

The fork carries `DYLD_PRINT_NOTIFICATIONS` (`dyld3/Logging.cpp:163`,
`_dyld_objc_notify_mapped` / `notifyBatch`). But the live dyld crashes at
`0x10005e6d4` before mapping any image (prior runs), so it cannot print
the registration on the live dyld — the rebuild is needed. No
rebuild-free answer.

## Path 3 — the dedup step

`build-dyld-only.sh` now patches the **worktree copy** of
`src/external/dyld/src/glue.c` (the salvage and the superproject are
untouched): an idempotent python step deletes the definitions whose
linker symbols ld64.lld reports as duplicates. The list is the measured
duplicate set (source spelling, one fewer leading underscore than the
linker symbol):

```
memset __stderrp uuid_unparse_upper
_Block_object_assign _Block_object_dispose
_NSConcreteGlobalBlock _NSConcreteStackBlock
```

It handles the `#ifdef DARLING … #else … #endif` around
`_Block_object_assign` (span = guard line → matching `}`).

## Build: OK

```
removed: __stderrp memset _NSConcreteStackBlock _NSConcreteGlobalBlock _Block_object_assign _Block_object_dispose uuid_unparse_upper
raw:     Mach-O 64-bit x86_64 executable …
patched: Mach-O 64-bit x86_64 dynamic linker, flags:<…>
LOG PATCH strings present: LOG PATCH: registerObjCNotifiers mapped=%p
```

The chain converged: all 7 duplicates removed, the link succeeds, no
`undefined` — so none of the removed fallbacks was needed.

## Gate: RED

New dyld into a **copy** of the overlay (`cp -al` + replace
`usr/lib/dyld`; the live overlay is untouched), `DARLING_SMOKE_REFRESH=1`,
`guest-wl-session-roundtrip` ×2:

```
run 1 rc=139 step12=0 DNR=0
run 2 rc=139 step12=0 DNR=0
```

Both segfault before `[step 01]`:

```
[darling-mldr] DEBUG pre-start: mh=0x826009000 entry=0x82716c780 …
[darling-mldr] FATAL signal 11 (code=1) at addr=0x1000fce90
```

The rebuilt dyld does not run the guest — the same class as the earlier
rebuilt-and-fixed dyld (which also got past the header defects and then
died inside mldr). The LOG PATCH lines (`registerObjCNotifiers`,
`calling sNotifyObjCMapped`) are not reached.

## Verdict (one line)

**Gate red** — the dedup chain converged (7 duplicates removed, link OK,
MH_DYLINKER, LOG PATCH strings present), but the rebuilt dyld segfaults
the guest (`FATAL signal 11 at addr=0x1000fce90`) before `[step 01]` in
both runs, so the LOG PATCH localisation of `notifyBatchPartial+0x919`
is not reached; the rebuilt dyld itself is the blocker.
