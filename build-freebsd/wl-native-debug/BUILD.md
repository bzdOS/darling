# Debug copy of native libwayland-client 1.25.0 — roundtrip-body markers

Instrumented copy of the native `libwayland-client` used for one question:
where a `wl_display_roundtrip` issued on a spawned foreign thread parks,
inside a process launched through the darling guest loader. The copy is
**never installed** and never replaces the system library; it exists only as
an artifact of this directory plus the source recipe below.

## Contents

- `instrument-body.py` — inserts 13 anchor-asserted stderr markers
  (`[wlbody] #N tid=... step ...`) into wayland 1.25.0 sources. Anchor
  assertions guarantee the loop bodies and the waits are untouched: the
  script fails if an anchor is missing or duplicated.
- `wl-debug-body.patch` — the same insertions as a reviewable diff
  (regenerate: `python3 instrument-body.py` in a clean source tree, then
  `git diff`).
- `config.h.generated` — the `config.h` produced by meson's feature checks
  on this OS (no meson on the build node; the values were derived by hand
  from the same checks meson performs — see the script's header comment).

## Reproduce

```
git clone --depth 1 --branch 1.25.0 https://gitlab.freedesktop.org/wayland/wayland.git wl-debug-src  # HEAD 3e673a43
cd wl-debug-src && python3 <this-dir>/instrument-body.py
wayland-scanner -s client-header      protocol/wayland.xml bld/wayland-client-protocol.h
wayland-scanner -s client-header -c   protocol/wayland.xml bld/wayland-client-protocol-core.h
wayland-scanner -s public-code        protocol/wayland.xml bld/wayland-protocol.c
cp <this-dir>/config.h.generated bld/config.h
cc -shared -fPIC -O2 -fvisibility=hidden -I bld -I src \
   -I/usr/local/include -I/usr/local/include/libepoll-shim \
   -o bld/libwayland-client.so.0.25.0 \
   src/wayland-client.c src/connection.c src/wayland-os.c src/wayland-util.c bld/wayland-protocol.c \
   -Wl,-soname,libwayland-client.so.0 -L/usr/local/lib -lepoll-shim -lffi -lpthread -lm
```

Resulting copy (this slice): 88536 bytes, sha256
`ac32f46df8fe5f306c01c94529a590485474c2ba9a523c6d891c208054d0e214`;
exported surface verified 65/65 identical to the system library
(`/usr/local/lib/libwayland-client.so.0.25.0`, 71600 bytes, wayland 1.25.0).

## Per-process substitution — what was measured, and what failed

The library is resolved by the **guest loader's own walker** (elfcalls
`lm_find` chain inside mldr), not by the host dynamic linker. The walker's
search list, measured with `LD_DEBUG=files` on the guest run, is:

```
Searching for "libwayland-client.so.0"
  <relative dyld components — NOT formed in the current run state, see below>
search_library_pathfds('libwayland-client.so.0', ...)
  Trying "/lib/libwayland-client.so.0"
  Trying "/usr/lib/libwayland-client.so.0"
  Trying "/usr/local/lib/libwayland-client.so.0"
  Opened "/usr/local/lib/libwayland-client.so.0"      <- the system copy, every run
```

Substitution levers tried for one guest run, each with the copy planted and
sha-verified beforehand; **all failed to divert the walk** (the `Opened`
line stayed the system path in every run):

1. `LD_LIBRARY_PATH=<dir-with-copy>` — the walk does not read it (this was
   the working assumption before the first measurement; it is refuted).
2. `DYLD_LIBRARY_PATH=<dir-with-copy>` — same: absolute-only walk.
3. A relative `usr/lib/libwayland-client.so.0` in the process CWD — the
   relative component was never formed, so the file was never tried.
4. The copy planted at the vchroot-relative `usr/lib/` of the run's staging
   tree (`$DYLD_ROOT_PATH/usr/lib/libwayland-client.so.0`, sha-verified
   `ac32f46d...`) — the relative `usr/lib` component did not appear in the
   walk in the current run state, so the planted file was never tried.

An earlier trace of the same walker (same binary, same call site) DID show
the relative components — `ibrary/Frameworks`, `System/Library/PrivateFrameworks`,
`usr/lib` — walked before `search_library_pathfds`; the `usr/lib` entry is
the vchroot-relative candidate that would have caught the planted copy. In
the runs of this slice that phase was not formed (sometimes one mangled
component appeared, e.g. `F-8/...`). The relative-phase list is therefore
state/env-dependent in a way not pinned by measurement; pinning it further
requires instrumentation inside the walker (guest loader side), which is out
of scope for a library-side slice.

Consequence: with the substitution levers above exhausted, the body of
`wl_display_roundtrip` cannot be observed from the guest run without either
(a) loader-side instrumentation of the walk, or (b) rebuilding the guest
shim to `dlopen` an absolute debug-copy path (probe-side change). The
markers themselves are in place and verified in the binary — the moment the
copy opens, `[wlbody]` lines appear on stderr.

Markers are fprintf-only: no behaviour change (anchor assertions in
`instrument-body.py` guarantee it).
