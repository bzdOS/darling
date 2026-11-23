# r24 unknown SIGTRAP page owner (0x226605 / 0x225bd1 / 0x22dbec)

Scope: static log+code analysis only, no VM/runtime changes. Commands use placeholders:
- `$LOG` = `chrome-sr-r24.log`
- `$SRC` = darling source tree root

## 1) r24 image ranges and external-status confirmation

Command:
```sh
python3 - <<'PY'  # parse dyld-trace add-image/mh lines from $LOG
# prints mh, slide, name for all dyld-traced images
PY
```

Observed dyld-trace images include (excerpt with key bases):
```text
0x826b7f000 slide 0x726b7f000 chrome-macho
0x354ddb687000 slide 0x354ddb687000 darling-bootstrap.dylib
...
0x354ddc370000 slide 0x354ddc370000 libsystem_kernel.dylib
...
0x354ddd353000 slide 0x354ddd353000 Google Chrome for Testing Framework
...
0x354dee4c3000 slide 0x354dee4c3000 libxkbcommon.dylib <-NEW
count 274
```

Executable mapping patch log (patch_linux_raw_syscalls) has no range covering cluster addresses:
```sh
python3 - <<'PY'  # scan "executable mapping at ..." ranges
cluster=[0x226605,0x225bd1,0x22dbec]
# print hits
PY
```
Output:
```text
0x226605 hits 0
0x225bd1 hits 0
0x22dbec hits 0
```

Direct log citations for cluster addresses:
```text
$LOG:93    [darling-mldr] SIGTRAP-AUDIT: rt_sigaction(5) from 0x226605 <unknown> old=0x0 new=0x82854b940
$LOG:182   [darling-mldr] SIGTRAP-AUDIT: rt_sigaction(5) from 0x226605 <unknown> old=0x82854b940 new=0x354ddc39bbe0
$LOG:25095 [darling-mldr] SIGTRAP-AUDIT: rt_sigaction(5) from 0x226605 <unknown> old=0x354ddc39bbe0 new=0x0
$LOG:25096 [darling-mldr] SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=0x354ddc39bbe0 (linux_signo=5) rip=0x22dbec si_code=65537 si_addr=0x0
$LOG:25098 [darling-mldr] SIGTRAP-AUDIT-CHAIN: #0 ret=0x225bd1
$LOG:25102 [launch] GUEST-EXIT pid=11888 status=0x5 t=24s
```

Range check against dyld-trace images: lowest traced image base is `0x825ee2000` (crashpad handler), main executable is `0x826b7f000` (slide `0x726b7f000`), Framework is `0x354ddd353000` (slide same as mh, vmaddr 0). Addresses `0x225bd1/0x226605/0x22dbec` are below every dyld-trace mh range and not inside chrome-macho `__TEXT` (`vmaddr 0x100000000 size 0x4000`, unslid text ends at `0x100004000`; unslid+slide for 0x22dbec would be `0x826dacbec`, but raw rip logged is `0x22dbec`). Therefore the cluster is **outside dyld-registered image ranges** in r24.

## 2) mldr/dyld executable pages mapped outside dyld registration

### mldr patch points (registered/executable image patching)

- `$SRC/src/startup/mldr/loader.c:294-296` patches dylinker executable segment after mapping:
  ```c
  if (expect_dylinker && (useprot & PROT_EXEC))
      mldr_patch_linux_raw_syscalls((void*)addr, seg->filesize);
  ```
- `$SRC/src/startup/mldr/freebsd_syscall_trap.c:2371-2376` patches file-backed executable mmap results and records mapping:
  ```c
  if (mr >= 0 && (int)a5 >= 0) {
      bool executable = (a3 & PROT_EXEC) != 0;
      mldr_note_mapping((void *)mr, (size_t)a2, (int)a5, executable);
      if (executable)
          mldr_patch_linux_raw_syscalls((void *)mr, (size_t)a2);
  }
  ```
- `$SRC/src/startup/mldr/freebsd_syscall_trap.c:3965-4025` `mldr_patch_linux_raw_syscalls` rewrites matching trampoline signatures to `ud2`, logs patch/no-match, restores `PROT_READ|PROT_EXEC`. It only patches existing mapped pages; it does **not** allocate a new executable page.
- `$SRC/src/startup/mldr/freebsd_syscall_trap.c:1595-1705` `mldr_arm_trap_at` makes targeted pages temporarily RWX to plant `ud2` traps for diagnostics (env-controlled); also no new executable allocation.

### dyld/mldr anonymous executable allocations

Searched for `PROT_EXEC` mmap/JIT/trampoline allocation in `$SRC/src/startup/mldr` and `$SRC/src/external/dyld` (`grep -rn 'MAP_JIT|VM_FLAGS_MAP_JIT|jit|mmap.*PROT_EXEC|PROT_EXEC' ...`). Findings show PROT_EXEC checks/mprotect in mldr patching, Mach-O loaders, and dyld code; no mldr path creating anonymous executable trampolines/JIT pages outside dyld image mapping was identified. dyld hits are mostly permission checks/shared-cache mapping; no obvious anonymous executable trampoline page creation explaining low fixed addresses `0x22xxxx`.

## 3) Verdict

**Not found / not attributable from current r24 log and mldr/dyld static sources.**

Evidence:
- Cluster addresses `$LOG:93,182,25095,25096,25098` (`0x226605/0x225bd1/0x22dbec`) are outside all 274 dyld-trace image ranges and outside all logged executable-mapping patch ranges (0 hits).
- mldr patches executable dylinker/mmap pages in place (`loader.c:294-296`, `freebsd_syscall_trap.c:2371-2376`, `3965-4025`) and does not allocate anonymous executable pages for trampolines.
- No JIT/MAP_JIT/anonymous PROT_EXEC trampoline owner was identified in mldr/dyld for these low addresses.

Cause of non-identification: r24 log lacks a vmmap/proc mapping dump at SIGTRAP time; addresses are below traced image bases and cannot be mapped to a dylib/file-backed segment or to an mldr-allocated executable page with available evidence. A targeted runtime mapping dump (vm_map/procstat/ktrace mmap) around the trap would be needed to name the owning page.
