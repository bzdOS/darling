# dyld-logged: muldefs rebuild (wall #6, path 2)

Wall #6. 9.9 is removed for the loaded set (trie census); the frontier is
`notifyBatchPartial+0x919`, and localising it needs a LOG PATCH dyld.
Path 2: rebuild the diagnostic dyld tolerating the `glue.c` fallbacks
that duplicate the static libraries.

## The flag and where it must go

`build-dyld-only.sh` seeds `-Wl,--allow-multiple-definition` into
**`CMAKE_EXE_LINKER_FLAGS_SAVED`**. The dyld `CMakeLists.txt` overwrites
`CMAKE_EXE_LINKER_FLAGS` with `"${CMAKE_EXE_LINKER_FLAGS_SAVED}
-nostdlib"` (line 83), so a plain `-DCMAKE_EXE_LINKER_FLAGS=…` is
discarded before the link — the flag must be in `_SAVED`. Verified: the
`system_loader` link line in `build.ninja` now carries
`-Wl,--allow-multiple-definition`.

## Result: the linker rejects the flag

```
ld64.lld: error: unknown argument '--allow-multiple-definition'
clang++: error: linker command failed with exit code 1 (use -v to see invocation)
ninja: build stopped: subcommand failed.
```

The linker is **ld64.lld** — the `-fuse-ld=` cctools-port path
(`…/cctools/ld64/src/x86_64-apple-darwin20-ld`) is a symlink to
`/usr/local/bin/ld64.lld`. It has no multiple-definition switch:
`--dead-strip-duplicates` (already on the line) covers only symbols that
will be dead-stripped, and `-multiply_defined,suppress` is not accepted
either. So path 2 is blocked at the linker and the gate is not reached.

## Verdict (one line)

**Gate red: muldefs rejected** — `ld64.lld: unknown argument
'--allow-multiple-definition'`; the flag was injected correctly (via
`CMAKE_EXE_LINKER_FLAGS_SAVED`) but ld64.lld has no multiple-definition
switch, so the logged rebuild does not link (exact error above); no third
path in budget.
