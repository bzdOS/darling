#!/usr/bin/env python3
# check-wayland-window-probe.py — verify tests/src/wayland-window-create.m
# against the built Wayland.backend dylib BEFORE running it in the guest.
#
# WHY: the backend's sources were never committed, so that test declares the
# private methods it calls itself. Launching a guest process needs root, and
# the first thing an ABI mismatch does is abort with an unrecognised selector
# or a smashed argument -- which costs a root-run to discover and looks like
# a backend bug. Every declaration in the test is therefore checked here
# against the encodings actually present in the dylib's ObjC metadata.
#
# USAGE: python3 build-freebsd/check-wayland-window-probe.py <Wayland-dylib>
#
#   The dylib is the one installed in the overlay:
#     <overlay>/System/Library/Frameworks/AppKit.framework/Versions/C/Resources/
#              Backends/Wayland.backend/Contents/MacOS/Wayland
#   It is deliberately not committed (it is the only copy in existence), so
#   it is passed in rather than hardcoded.
#
# EXIT: 0 if every declaration matches, 1 otherwise.
#
# The encodings the test declares, and where each one comes from:
#
#   shm                             ^{wl_shm=}16@0:8                 test: WLDisplayProbe
#   compositor                      ^{wl_compositor=}16@0:8          test: WLDisplayProbe
#   wmBase                          ^{xdg_wm_base=}16@0:8            test: WLDisplayProbe
#   newWindowWithDelegate:          @24@0:8@16                        test: NSDisplay (AppKit decl)
#   screens                         @16@0:8                            test: NSDisplay (AppKit decl)
#   setFrame:                       v48@0:8{CGRect}16                  test: CGWindow (AppKit decl)
#   frame                           {CGRect}16@0:8                     test: CGWindow (AppKit decl)
#   styleMask                       Q16@0:8                            test: CGWindow (AppKit decl)
#   _acquireBackBufferForWidth:height: ^{?=^{wl_buffer}^vQiiiic}24@0:8i16i20
#                                               test: WLWindowProbe (struct return!)
#   flushBuffer                     v16@0:8                            test: WLWindowProbe
#   init                            @16@0:8                            via +[NSDisplay currentDisplay]
#
# windowNumber is NOT in the dylib and is expected to be absent: it comes
# from CGWindow (CoreGraphics/CGWindow.m:124, which returns `(NSInteger)
# self`). It was checked by hand against that source; it is listed here so
# its absence is recorded rather than looking like an oversight.

import struct
import sys

LC_SEGMENT_64 = 0x19
MH_MAGIC_64 = 0xFEEDFACF

# selector -> encoding the test relies on
EXPECTED = {
    "shm": "^{wl_shm=}16@0:8",
    "compositor": "^{wl_compositor=}16@0:8",
    "wmBase": "^{xdg_wm_base=}16@0:8",
    "newWindowWithDelegate:": "@24@0:8@16",
    "screens": "@16@0:8",
    "setFrame:": "v48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16",
    "frame": "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8",
    "styleMask": "Q16@0:8",
    "_acquireBackBufferForWidth:height:": "^{?=^{wl_buffer}^vQiiiic}24@0:8i16i20",
    "flushBuffer": "v16@0:8",
    # WaylandDisplay implements -init itself (not inherited): that is the
    # method that does wl_display_connect + the registry roundtrip, and
    # +[NSDisplay currentDisplay] reaches it. Worth pinning, because if it
    # were ever dropped the backend would return a half-built display.
    "init": "@16@0:8",
}

# Must NOT be implemented by the backend; inherited instead (see above).
EXPECT_ABSENT = ("windowNumber",)


class MachO:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", self.d, 0)
        if magic != MH_MAGIC_64:
            raise SystemExit("not a 64-bit Mach-O: %08x" % magic)
        self.sections = {}
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.d, off)
            if cmd == LC_SEGMENT_64:
                nsects = struct.unpack_from("<I", self.d, off + 64)[0]
                so = off + 72
                for _i in range(nsects):
                    sectname = self.d[so:so + 16].rstrip(b"\0").decode()
                    segname = self.d[so + 16:so + 32].rstrip(b"\0").decode()
                    addr, size, foff = struct.unpack_from("<QQI", self.d, so + 32)
                    self.sections.setdefault(sectname, (addr, size, foff))
                    so += 80
            off += cmdsize

    def to_fo(self, vmaddr):
        for _n, (addr, size, foff) in self.sections.items():
            if addr <= vmaddr < addr + size:
                return foff + (vmaddr - addr)
        return None

    def cstr(self, vmaddr):
        fo = self.to_fo(vmaddr)
        if fo is None:
            return "<?>%x>" % vmaddr
        end = self.d.index(b"\0", fo)
        return self.d[fo:end].decode("utf-8", "replace")


def methods(m):
    """selector -> list of (class, encoding)"""
    out = {}
    addr, size, foff = m.sections["__objc_classlist"]
    for i in range(size // 8):
        cls = struct.unpack_from("<Q", m.d, foff + i * 8)[0]
        cfo = m.to_fo(cls)
        if cfo is None:
            continue
        ro = m.to_fo(struct.unpack_from("<Q", m.d, cfo + 32)[0] & ~1)
        if ro is None:
            continue
        name_ptr, base_methods = struct.unpack_from("<QQ", m.d, ro + 24)
        cname = m.cstr(name_ptr)
        if not base_methods:
            continue
        mfo = m.to_fo(base_methods)
        if mfo is None:
            continue
        _entsize, cnt = struct.unpack_from("<II", m.d, mfo)
        for k in range(cnt):
            sel, types, _imp = struct.unpack_from("<QQQ", m.d, mfo + 8 + k * 24)
            out.setdefault(m.cstr(sel), []).append((cname, m.cstr(types)))
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    found = methods(MachO(path))
    print("dylib: %s" % path)
    print("classes: %d, distinct selectors: %d\n"
          % (len(set(c for v in found.values() for c, _t in v)), len(found)))

    bad = 0
    for sel in sorted(EXPECTED):
        want = EXPECTED[sel]
        hits = found.get(sel, [])
        if not hits:
            print("MISSING  %-34s expected %s" % (sel, want))
            bad += 1
            continue
        for cname, got in hits:
            ok = "ok  " if got == want else "DIFF"
            if got != want:
                bad += 1
            print("%s  %-34s %-16s %s" % (ok, sel, cname, got))
            if got != want:
                print("      expected: %s" % want)

    for sel in EXPECT_ABSENT:
        if sel in found:
            print("UNEXPECTED %-31s implemented by %s (test assumes it is inherited)"
                  % (sel, found[sel]))
            bad += 1
        else:
            print("ok        %-34s absent, as expected (inherited)" % sel)

    print("\n%s: %d problem(s)" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
