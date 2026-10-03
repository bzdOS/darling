#!/usr/bin/env python3
"""inject-init-markers.py — insert once-only write(2) markers into the
BUILD COPY of the pin sources (never the submodule) so the init-cascade
probe can localize the first fatal point:

  MSL-MARK init-enter         — top of __malloc_init
  MSL-MARK late-init-enter    — top of __malloc_late_init
  MSL-MARK malloc-entry       — top of malloc() (first call only)
  MSL-MARK zone-malloc-entry  — top of _malloc_zone_malloc (first only)

Usage: inject-init-markers.py <src-pin-dir>
"""
import re
import sys

MARKERS = [
    ("__malloc_init(const char *apple[])", "MSL-MARK init-enter\\n"),
    ("__malloc_late_init(const struct _malloc_late_init *mli)", "MSL-MARK late-init-enter\\n"),
    ("malloc(size_t size)", "MSL-MARK malloc-entry\\n"),
    ("_malloc_zone_malloc(malloc_zone_t *zone, size_t size, malloc_zone_options_t mzo)",
     "MSL-MARK zone-malloc-entry\\n"),
]


def main():
    src_dir = sys.argv[1]
    path = f"{src_dir}/malloc.c"
    txt = open(path).read()
    if "MSL-MARK" in txt:
        print("markers already present")
        return
    txt = txt.replace('#include "internal.h"',
                      '#include "internal.h"\nextern long write(int, const void *, unsigned long);', 1)
    for i, (sig, msg) in enumerate(MARKERS):
        pat = re.escape(sig) + r"\n\{"
        mark = (sig + "\n{\n"
                f"\tstatic volatile int _mm_once_{i};\n"
                f"\tif (!_mm_once_{i}) {{ _mm_once_{i} = 1; write(2, \"{msg}\", {len(msg)}); }}")
        txt, n = re.subn(pat, lambda _m: mark, txt, count=1)
        if n != 1:
            print(f"WARN: insertion point not found for: {sig}")
        else:
            print(f"marker injected: {msg.strip()} ({len(msg)} B)")
    open(path, "w").write(txt)


if __name__ == "__main__":
    main()
