# Staging candidates: the relative phase of the loader walk is gated on LD_LIBRARY_PATH

Question (repro `guest-wl-session-roundtrip-macho`, variant (a) of the session
probe): the host-loader walk at a guest `dlopen("libfreetype.so.6")` sometimes
tries RELATIVE components before the absolute phase —

```
dlopen_object name "libfreetype.so.6" fd -1 refobj ".../mldr" lo_flags 0x2 mode 0x1
 Searching for "libfreetype.so.6"
  Trying "ibrary/Frameworks/libfreetype.so.6"          <- "System/Library/Frameworks" minus "System/L"
  Trying "System/Library/PrivateFrameworks/libfreetype.so.6"
  Trying "usr/lib/libfreetype.so.6"
search_library_pathfds('libfreetype.so.6', '(null)', fdp)
  Trying "/lib/libfreetype.so.6"
  Trying "/usr/lib/libfreetype.so.6"
  Trying "/usr/local/lib/libfreetype.so.6"
```

— and sometimes goes straight from `Searching for` to the absolute phase. The
components are the run's staging list (`System/Library/Frameworks`:
`System/Library/PrivateFrameworks`:`usr/lib`) with the FIRST entry stripped of
exactly 8 bytes ("System/L") and the rest intact. What state turns the phase
on?

## Reproduction

Harness and probe as in the window-probe script; one variable per run, all
runs share the seat, the closure-derived staging list and `LD_DEBUG`:

```
sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY \
    DARLING_BUILD_DIR=$DARLING_BUILD_DIR \
    DARLING_TEST_BINARY=guest-wl-session-roundtrip-macho \
    DARLING_STAGING_TREES='System/Library/Frameworks:System/Library/PrivateFrameworks:usr/lib' \
    LD_DEBUG=all \
    XDG_RUNTIME_DIR=<wayland-runtime> WAYLAND_DISPLAY=wayland-1 \
    timeout --foreground -k 5 180 $DARLING_BUILD_DIR/launch-dynamic \
    > wl-body-pin<N>.log 2>&1
```

Criterion per log: at the `libfreetype` `dlopen_object`, are relative
components tried before `search_library_pathfds`?

## Matrix (one variable per run)

| pin | variable changed (relative to baseline)      | freetype dlopen reached | relative components |
|-----|-----------------------------------------------|-------------------------|---------------------|
| 0   | none — baseline                               | yes                     | NO                  |
| 1   | `DARLING_STAGING_TREES=` (empty)              | no (closure too thin)   | n/a                 |
| 2   | `DARLING_STAGING_TREES=usr/lib`               | no (closure too thin)   | n/a                 |
| 3   | staging list reversed                         | yes                     | NO                  |
| 4   | CWD of the run changed (`/tmp`)               | yes                     | NO                  |
| 5   | local overlay cache dumped before the run     | yes                     | NO                  |
| 6   | local overlay cache rebuilt from scratch      | yes                     | NO                  |
| 7   | `LD_LIBRARY_PATH=<dir with instrumented copy>`| yes                     | **YES**             |

pins 1-2 cannot reach the criterion at all: with a thin staging list the
guest dies before the backend fonts load, so "no candidates" there is not
evidence about the phase. pins 0 and 3-6 reached the dlopen and all show the
absolute-only walk.

## Condition

**ON if and only if `LD_LIBRARY_PATH` is non-empty in mldr's environment at
exec.** The value itself is not special-cased in the observed data (the one
ON run used the instrumented-copy directory); `LD_DEBUG` does not form the
phase, it only makes the walk visible in the log (the baseline control has
`LD_DEBUG` set and shows no phase). Staging-list value, element order, run
CWD, and local-overlay cache state do not form it.

Excerpt, ON run (`LD_LIBRARY_PATH` set):

```
 Searching for "libwayland-client.so.0"
  Trying "ibrary/Frameworks/libwayland-client.so.0"
  Failed to open "ibrary/Frameworks/libwayland-client.so.0": No such file or directory
  Trying "System/Library/PrivateFrameworks/libwayland-client.so.0"
  Failed to open "System/Library/PrivateFrameworks/libwayland-client.so.0": No such file or directory
  Trying "usr/lib/libwayland-client.so.0"
  Failed to open "usr/lib/libwayland-client.so.0": No such file or directory
search_library_pathfds('libwayland-client.so.0', '(null)', fdp)
  Trying "/lib/libwayland-client.so.0"
  ...
```

Excerpt, OFF run (`LD_LIBRARY_PATH` unset, everything else equal):

```
 Searching for "libfreetype.so.6"
search_library_pathfds('libfreetype.so.6', '(null)', fdp)
  Trying "/lib/libfreetype.so.6"
  Trying "/usr/lib/libfreetype.so.6"
  Trying "/usr/local/lib/libfreetype.so.6"
  Opened "/usr/local/lib/libfreetype.so.6", fd 3
```

## Code read: where the list is NOT built

`src/startup/mldr/loader.c` read in full (435 lines): it is a Mach-O mapper —
segments, `LC_UNIXTHREAD`, `LC_MAIN`, `LC_UUID`, and `LC_LOAD_DYLINKER`
whose only path construction concatenates `lr->root_path` with the dylinker
path (lines 355-370). There is no library search-list builder in it.

`src/startup/mldr/elfcalls/elfcalls.c`: `dlopen_simple` (lines 17-19) is a
plain `dlopen(name, RTLD_LAZY)`; `dlopen_fatal` (lines 22-25) wraps it. The
guest's bare-name host `dlopen` lands in the host rtld of the mldr process
unchanged.

`mldr.c` reads only `__mldr_*` variables (lines 586-618) and the vchroot path
RPC (lines 1185-1200); no staging variable, no search list.

The only code in this tree that reads `DARLING_STAGING_TREES` is the harness
(`tests/launch-dynamic-smoke.c` lines 604-610): it prints the value and stages
the trees into the local overlay; it never touches the loader's search list.

So the relative phase is a property of the host rtld search in the mldr
process: with `LD_LIBRARY_PATH` set at exec, the per-object path phase of
`dlopen_object` is formed and contains the staging-derived relative
components; with it unset, `dlopen_object` goes straight to
`search_library_pathfds`. The exact producer of the STRIPPED list (first
entry minus 8 bytes — consistent with stripping a prefix once from the joined
colon list before splitting) is not present in this repository: no source or
shipped binary here contains the staging variable except the harness. To name
the producing function next: instrument the walker side (rtld search trace at
the first guest-phase `dlopen`) or read the overlay dyld sources from the
overlay's own provenance tree.

## Note on the phase's usefulness

The relative phase is the only walk that would open a copy planted at the
vchroot-relative `usr/lib/` of the staging tree; the absolute phase never
does. Pinning the phase on (item 7's environment) is therefore the lever that
makes a planted instrumented `libwayland-client.so.0` observable from the
guest run.
