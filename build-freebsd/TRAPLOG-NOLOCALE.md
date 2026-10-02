# traplog-nolocale: the locale-free marker path opens the window — and
# names the park

Lane bsdos-x86-31. The marker path of mldr (`mldr_tlog`/`mldr_tlogx`
and the gate-gated SIGSYS/SIGILL narration) now formats with
hand-rolled decimal/hex converters and emits with `write(2)` — no
stdio on the marker path at all (the fault object of the
survive-window slice was the locale data read inside libc's
`vfprintf` via `localeconv_l`; stdio formatting WAS the exposure).
Gates, call-site signatures, the ungated prints and the crash path's
FATAL print are untouched. Converter and drop-in proof landed first:
`build-freebsd/traplog-formats-test.c` (6/6 byte-exact, RC=0) and a
drop-in compile of every call-site shape in mldr (RC=0).

## (3) The gated run survives to the window

`wl-body-nolocale-t.log` (`DARLING_TRAP_LOG=1`, the previously dying
configuration): the run carries 61720 `[traplog]` markers and reaches

```
variant (a): main at rest, spawned lane runs wl_display_roundtrip
lane: DID-NOT-RETURN within 8000ms — parked inside wl_display_roundtrip
```

— the (a) window, with markers live, and no FATAL anywhere. The
locale-free path removed the BUS_OBJERR: the fault was the marker
path's own stdio touching the locale data, and taking stdio off the
path takes the fault with it.

## (4) The two deferred measurements from task 26

**The parked lane's dlsym: COMPLETES.** The (a) lane (altstack OK,
tid 167686) shows `elf-dlsym ENTER` -> `elf-dlsym RETURN p=0x83a5ab3d0
b=0` — `elfcalls->dlsym_fatal` resolves the native symbol successfully
on the parked thread. The bridge resolution is NOT the park.

**The exit(3) caller: captured — and it is the clean termination.**
One `elf-exit CALL tid=102570 a=0` in the whole run — the main thread,
exit code 0, logged AFTER the VERDICT: the normal end-of-probe
process exit through the elfcalls slot. There is no park-time exit(3)
once the fault is gone — the earlier "exit-in-progress/P_WEXIT"
reading was the aftermath of the SIGBUS death, not a teardown the park
itself drives.

## The park site, finally named in the pure (a) condition

With the fault gone and the native copy caught, the (a) lane's trail
inside the instrumented libwayland is complete:

```
[wlbody] wrap ENTER tid=...670096
[wlbody] #68 roundtrip_queue ENTER
[wlbody] #69   set_queue
[wlbody] #70   sync returned callback
[wlbody] #71   dispatch-loop iter=0 ENTER
[wlbody] #72 dispatch_queue ENTER
[wlbody] #73   prepare_read_queue -> reader_count=1
[wlbody] #74     ppoll ENTER fd=8 events=1 timeout=NULL(infinite)
                     <- last marker; the lane never returns
```

The (a) park sits at **`ppoll(fd=8, POLLIN, timeout=NULL)` in the
native roundtrip's read path — the spawned lane IS the reader
(reader_count=1) and its infinite poll never becomes readable while
main is at rest.** This is the same read-path arbitration named in the
earlier (c) slices, now reproduced in the pure (a) condition with the
instrumentation intact.

Observation: in this run variant (d) also parks (`parked on a FRESH
display too`) while (c)'s main lane returns 2 — with the fault gone
the park structure shows BOTH spawned opaque roundtrips parking in the
native body; worth a dedicated comparison in the next slice.

## (5) Control: default unchanged

`wl-body-nolocale-a.log` (gate off): zero `[traplog]` markers, the
window reached, VERDICT printed — the default path behaves exactly as
the evening's baseline controls.

## (6) Control (c)

Present in the same gated log: the (c) main lane roundtrip returned 2
(errno 0) — the control arm intact under the new marker path.
