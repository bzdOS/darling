# dyld-rebuild: the rebuild attempt and the exports trie

Wall #6 (PLAN.md 9.9–9.10): dyld crashes in `ImageLoader::trieWalk+0xa4`,
suspected to be a mis-parsed trie node (reexport chain / absolute symbol
/ weak-def).

## Rebuild attempt

`sh build-freebsd/build-dyld-only.sh` applies the salvage files to the
dyld submodule worktree, configures the tree with cmake/ninja and builds
the `system_loader` target, then runs `fixup-dylinker.sh` to turn the
linked `MH_EXECUTE` into an `MH_DYLINKER` image.

The configure succeeds (top-level tree, `DARLING_OVERLAY` set). The link
fails:

```
ld64.lld: error: duplicate symbol: _memset
>>> defined in glue.c:396 .../dyld/src/glue.c
>>>            .../system_loader.dir/src/glue.c.o
>>> defined in src/external/libplatform/libplatform_static64.a(bzero.c.o)
ld64.lld: error: duplicate symbol: ___stderrp
ld64.lld: error: duplicate symbol: _uuid_unparse_upper
ld64.lld: error: duplicate symbol: __Block_object_assign
ld64.lld: error: duplicate symbol: __Block_object_dispose
```

`glue.c`'s "libc.a sometimes missing …" fallbacks now duplicate the
static libraries the target links. Removing the `_memset` and
`_NSConcrete*Block` fallbacks surfaces the next duplicates, so it is a
chain. The September `glue.c` that resolved this is **not** in the
salvage — `dyld-salvage/` carries only `dyld2.cpp`,
`dyldFreeBSDRebase.c`, `dyldInitialization.cpp`, `sandbox-dummy.c`,
`CMakeLists.txt` — so the rebuild does not come up within the budget.
The LOG PATCH itself is intact in the salvage (`registerObjCNotifiers`
@4749, `calling sNotifyObjCMapped` @1323).

## Fallback: the staged Chrome framework's LC_DYLD_EXPORTS_TRIE

Framework (in the overlay, not touched):
`Frameworks/Google Chrome for Testing Framework.framework/…/Google Chrome
for Testing Framework` — 267 MB, `MH_DYLIB`. `LC_DYLD_EXPORTS_TRIE`:
dataoff 266895552, datasize **80**.

Walked as a ULEB trie (node = terminal-size ULEB, terminal, child count,
(edge, child-offset)*):

```
nodes = 5
  _Chrome.AppModeStart_v8           flags=0x00 (regular)  addr=0x2840
  _Chrome.WebAppShortcutCopierMain  flags=0x00 (regular)  addr=0x2a00
  _Chrome.Main                      flags=0x00 (regular)  addr=0x3fe0
```

No `ABSOLUTE` (kind 2), no `WEAK_DEFINITION` (0x04), no `REEXPORT`
(0x08), no child offset out of bounds. The exports trie is clean.

## Verdict (one line)

The rebuild did not come up (link: duplicate `glue.c` fallbacks vs the
static libs); the `LC_DYLD_EXPORTS_TRIE` fallback is **clean** — 5 nodes,
3 regular exports, no suspicious type/offset — so hypothesis 9.9 is not
confirmed on the exports trie.
