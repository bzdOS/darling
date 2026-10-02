# dyld trie census: LC_DYLD_EXPORTS_TRIE over the staged overlay

Wall #6 (PLAN.md 9.9). 9.9 was cleared for the Chrome framework alone
(e3589130d: 5 nodes, 3 regular). The chrome-probe crash
(`notifyBatchPartial+0x919` / `trieWalk+0xa4`) could sit in the exports
trie of ANY image dyld loads before it — so census every Mach-O.

## Method

`sh build-freebsd/trie-census.sh` — walks every Mach-O under the staged
overlay (`$DARLING_OVERLAY`, read-only), parses the header and
`LC_DYLD_EXPORTS_TRIE`, walks the ULEB trie (method per
`build-freebsd/DYLD-REBUILD.md`) and reports per image: filetype,
dataoff/datasize, node count, export count, special flags (ABSOLUTE
kind=2 / WEAK 0x04 / REEXPORT 0x08) and out-of-bounds child offsets.

## Result

**555 Mach-O images scanned; 15 carry an `LC_DYLD_EXPORTS_TRIE`.**
Those 15:

| image (basename) | ft | dataoff | size | nodes | exports | special | oob |
|---|---|---|---|---|---|---|---|
| Google Chrome for Testing Framework | 6 | 266895552 | 80 | 5 | 3 | 0 | 0 |
| Libraries/libvulkan.dylib | 6 | 459936 | 5744 | 372 | 265 | 0 | 0 |
| Libraries/libvk_swiftshader.dylib | 6 | 4410040 | 152 | 8 | 4 | 0 | 0 |
| Libraries/libaperitif.dylib | 6 | 38048 | 40 | 2 | 1 | 0 | 0 |
| Helpers/web_app_shortcut_copier | 2 | 12408 | 32 | 2 | 1 | 0 | 0 |
| Helpers/chrome_crashpad_handler | 2 | 1474248 | 280 | 12 | 7 | **1** | 0 |
| Helpers/app_mode_loader | 2 | 800888 | 48 | 4 | 2 | 0 | 0 |
| Helpers/…Helper (Aperitif Renderer) | 2 | 12424 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (Alerts) | 2 | 38056 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper | 2 | 38056 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (GPU) | 2 | 38056 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (Aperitif GPU) | 2 | 12424 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (Renderer) | 2 | 38056 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (Aperitif) | 2 | 12424 | 16 | 2 | 1 | 0 | 0 |
| Helpers/…Helper (Aperitif Alerts) | 2 | 12424 | 16 | 2 | 1 | 0 | 0 |

The single special node:

```
chrome_crashpad_handler :
  __RNvCs8YuObao1ZSc_7___rustc35___rust_no_alloc_shim_is_unstable_v2 : WEAK : flags=0x4
```

`chrome_crashpad_handler` is a helper (filetype 2, `MH_EXECUTE`) — a
separate process, not a dylib the main framework process loads. No
ABSOLUTE, no REEXPORT, and **0 out-of-bounds child offsets** anywhere.

## Verdict (one line)

Special nodes: **chrome_crashpad_handler → WEAK
`__rust_no_alloc_shim_is_unstable_v2` (flags=0x4)** — an unloaded helper,
not a loaded dylib; among the loaded framework/`Libraries` images there
are **no** special nodes and **no** OOB, so 9.9 is removed for the loaded
set; the frontier is `notifyBatchPartial` (localisation needs the
rebuild — first attempt `-Wl,--allow-multiple-definition`; the September
`glue.c` is not in the salvage).
