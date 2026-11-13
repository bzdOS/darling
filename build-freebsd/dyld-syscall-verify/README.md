# dyld syscall verification (first step)

Artifact scope: reproduction command + raw/excerpt log
with >=1 measured syscall number on the internal _linux_syscall / overlay-dyld
trap path.

## Reproduction command (source)

Existing probe run script/log in build tree:
- log: /opt/darling/build/wl-body-dispexit.log (75671 lines)
- markers: `[traplog] SIGILL ENTER/LEAVE ... b=<linux_nr>` and
  `[traplog] linux-unhandled tid=... a=<linux_nr> b=<arg1>` emitted by
  `src/startup/mldr/freebsd_syscall_trap.c` (`DARLING_TRAP_LOG=1` path;
  unhandled raw Linux syscalls report `a=<linux_nr>` in `dispatch_linux_syscall`).

Representative command shape for this class of guest probe run (Wayland/session
roundtrip via launch-dynamic + overlay dyld/mldr, trap logging enabled):

```sh
cd /tmp/wlrun
sudo -n env DARLING_SRC_DIR="$DARLING_SRC_DIR" \
  DARLING_OVERLAY="$DARLING_OVERLAY" \
  DARLING_BUILD_DIR="$DARLING_BUILD_DIR" \
  DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
  DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
  LD_LIBRARY_PATH="$DARLING_BUILD_DIR/wl-debug-copy" WL_SKIP_B=1 DARLING_TRAP_LOG=1 \
  XDG_RUNTIME_DIR=/tmp/wayland-gwr WAYLAND_DISPLAY=wayland-1 \
  env LD_DEBUG=all \
  timeout --foreground -k 5 200 "$DARLING_BUILD_DIR/launch-dynamic" \
  > "$DARLING_BUILD_DIR/wl-body-dispexit.log" 2>&1
```

(See `build-freebsd/dyld-bindings-probe.sh` for the same launch/timeout + traplog
pattern; this artifact records the measured syscall numbers from the produced log.)

## Measured syscall numbers (raw excerpt)

From `/opt/darling/build/wl-body-dispexit.log` (`wl-body-dispexit.excerpt.txt`):

- Unhandled raw Linux syscall: `99` (`sysinfo`) observed repeatedly, e.g.
  `[traplog] linux-unhandled tid=102581 a=99 b=140737486256536`
  with preceding `[traplog] SIGILL ENTER ... b=99` and
  `[darling-mldr] unhandled Linux syscall 99 — ENOSYS` / `SIGILL LEAVE ... a=-78 b=99`.
- Unhandled raw Linux syscall: `267` (`readlinkat`) observed, e.g.
  `[traplog] linux-unhandled tid=102581 a=267 b=-100`
  with `[traplog] SIGILL ENTER ... b=267` and `SIGILL LEAVE ... a=-78 b=267`.
- Unhandled raw Linux syscall: `263` (`unlinkat`) observed, e.g.
  `[traplog] linux-unhandled tid=102581 a=263 b=-100`
  with `[traplog] SIGILL ENTER ... b=263` and `SIGILL LEAVE ... a=-78 b=263`.
- Handled/trapped Linux syscall numbers visible in same path include (examples):
  `228` (`clock_gettime`), `96` (`gettimeofday`), `1` (`write`), `309`
  (`getcpu`), `257` (`openat`), `217` (`getdents64`), `9` (`mmap`) with
  corresponding `SIGILL LEAVE a=<ret> b=<linux_nr>` lines.

Raw excerpts: `build-freebsd/dyld-syscall-verify/wl-body-dispexit.excerpt.txt`.
