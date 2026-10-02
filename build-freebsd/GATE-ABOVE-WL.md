# Gate above libwayland: the vendored dylib's per-call `__lazy` -> elfcalls dlsym

Question: variant (a) of the session repro — stateful session, main at rest,
spawned lane, opaque `wl_display_roundtrip` — parks ABOVE the native
library (the wrapper anchor stays silent on the parked lane while firing on
every other lane). Where exactly is the gate?

## Evidence 1 — the probe does not call the native function

The probe resolves its C surface with `dlsym(RTLD_DEFAULT, ...)`. From the
same run's log, the addresses:

```
[wayland_shim] dlsym_fatal('wl_display_roundtrip') => 0x8378593d0   <- native copy (base 0x83784e000)
         dlsym roundtrip=0x31ae74a712e0 get_error=0x31ae74a71330 ...  <- probe's pointer
```

0x31ae74a712e0 is NOT inside the native copy — it is the guest-side
vendored dylib (the AppKit Wayland backend's `Wayland` image, text base
0x31ae74a6b000) whose `_wl_display_roundtrip` sits at unslid offset 0x62e0
(0x31ae74a6b000 + 0x62e0 = 0x31ae74a712e0). So every call the probe makes,
including the parked variant (a), enters the VENDORED dylib first — the
native library is reached only through it. That is why the native wrapper
anchor can stay silent while the lane is stuck.

## Evidence 2 — what the vendored export does (disassembly)

`llvm-objdump -d` of `tests/vendor/wayland-backend/Wayland`:

```
_wl_display_roundtrip (0x62e0):
    [...]                       ; save display arg
    lea  <lazy symbol string>, %rdi
    call __lazy                 ; 0x5f00 — on EVERY invocation, not cached
    test %rax, %rax
    je   -> return -1
    mov  display, %rdi
    call *%rax                  ; native wl_display_roundtrip (would fire the wrap anchor)
```

`__lazy` (0x5f00):

```
    cmpq $0, __wl_handle        ; 0xd190
    jne  fast_path
    [...]                       ; first time: open the native library —
                                ; calls elfcalls slot 0x38 = dlopen_fatal
    call __load_interfaces
fast_path:
    mov  elfcalls_table, %rax   ; via 0xb028
    mov  0x48(%rax), %rax       ; slot 0x48
    mov  __wl_handle, %rdi
    mov  <symbol name>, %rsi
    call *%rax                  ; <- per-call bridge call
    [...]
```

`struct elf_calls` (`src/startup/mldr/elfcalls/elfcalls.h`) field order:
slot 0x38 = `dlopen_fatal`, **slot 0x48 = `dlsym_fatal`**
(`elfcalls.c:33-35` — host `dlsym` with a fatal wrapper). So on every
`wl_display_roundtrip` call the vendored dylib performs a FRESH host
`dlsym(handle, "wl_display_roundtrip")` through the elfcalls bridge, on the
calling thread.

## The gate, named

The parked variant-(a) lane never reaches the native wrapper and never
prints the dylib's own `[wayland_shim] dlsym_fatal(...) => %p` line (the
dylib prints it AFTER the bridge call returns; the (a) window has neither).
With the disassembly above, the park site is therefore inside the per-call
`__lazy` bridge invocation — the host `dlsym` performed by
`elfcalls->dlsym_fatal` on a DARLING-created guest thread. The lane is
alive and making raw syscalls up to that point (ENOSYS noise in its
window); variant (d) completes the identical chain on a fresh display, so
whatever blocks inside the bridge reads session state.

The remaining unknown is the exact primitive inside the host `dlsym` path
(rtld object lock / dlerror TLS / libthr bookkeeping for a host-foreign
thread). That is what the syscall trace of the parked tid names next.

## Truss does not survive the guest (negative control)

`truss -f` over the identical run kills it before the probe's first step:
the log ends in `thr_kill(<pid>, SIGILL)` — the NSException abort path —
while the untrussed run reaches the park every time. The ptrace-based
tracer interferes with the guest's signal/trap machinery (SIGSYS delivery
included), so syscall-level naming must come from dtrace (kernel probes,
no ptrace). This negative is measured, not assumed: same command, same
environment, only `truss -f` added.

## Dtrace: usable, but the dlsym probe is too hot to reach the window

`dtrace` runs on this kernel (`fbt` matches 45835 probes) and needs no
ptrace, so it was the replacement for truss. The script compiles and the
traced run starts, but any probe on `dlsym` entry (with `stack()`) slows
the process to a crawl: the guest dyld resolves its binds through the same
bridge constantly from startup, so the probe fires thousands of times
before the probe program reaches step 01, and the run dies on its timeout
with zero variants exercised (measured: three script revisions, `dlsym`
entry probe matched, 0 hits in the output while the run output never
printed a step line). Naming the primitive inside the host `dlsym` needs
TIME-GATED probes — enable the `dlsym` probes only inside the variant-(a)
window (a `tick`-scheduled enable/disable, or `lockstat` on the display's
object) — that is the next instrumentation design, not a re-run of this
one.

## Verdict

The gate of variant (a) is named by measurement: **the vendored dylib's
`__lazy` resolver — a fresh `elfcalls->dlsym_fatal` (host `dlsym`) call on
every `wl_display_roundtrip` invocation, executed on the DARLING-created
guest thread.** Everything above it (the probe's indirect call through its
own dlsym pointer) is instrument-free and fast; everything below it (the
native wrapper) never executes on the parked lane. The park is
session-state-dependent (variant (d) completes the same chain on a fresh
display) and the blocked primitive inside the host `dlsym` (rtld object
lock / dlerror TLS / libthr bookkeeping for a host-foreign thread) is the
one remaining unknown, with the time-gated dtrace named as the tool to
close it.

## Differential note

The probe-side differential (call the native pointer directly, bypassing
the dylib) is not constructible from the probe: its `dlsym(RTLD_DEFAULT)`
can only see the guest-side export table, and the native pointer lives
behind the elfcalls bridge the probe cannot reach. The address diff above
IS the differential result: the probe never holds the native pointer, and
the wrapper+`__lazy` is always in the path — so "gate in the shim export"
and "gate below it" collapse into one measured site: `__lazy`'s bridge
call.
