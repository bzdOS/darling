#!/usr/bin/env python3
# check-guest-dylib-compat.py — verify that every dylib a guest binary wants is
# actually present in the overlay at a compatibility version high enough.
#
# WHY THIS EXISTS
#
# The guest loader refuses a dylib whose compatibility version is lower than
# the one the loading binary requires. In dyld's own words, at
# dyld3/ClosureBuilder.cpp:372:
#
#     if ( (foundCompatVers < compatVersion) && mh->enforceCompatVersion() )
#         error("found '%s' which has compat version (%s) which is less than
#                required (%s).  Needed by '%s'", ...)
#
# so a mismatch is a load failure that happens before any of our code runs, and
# it reads like a missing library rather than a version problem. The probe
# links Foundation and AppKit, and the overlay's copies of those declare
# themselves as 65535.255.255 -- which is exactly what the probe requires, so
# it passes. That is worth checking rather than assuming: the versions are
# picked by the linker from whichever dylibs it finds, and the overlay is
# rebuilt from time to time.
#
# One trap that makes this harder to eyeball: llvm-objdump prints the
# version of a dylib whose version words are 0xFFFFFFFF as "current version
# n/a / compatibility version n/a", which looks like "no version" and is
# really the 65535.255.255 sentinel. Read the raw words instead.
#
# CLOSURE MODE (--closure)
#
# The version check above is the FIRST RING and it was the whole of preflight
# 3b for a long time, which is how a missing PrivateFrameworks got all the way
# to a root run: Onyx2D is a dependency of AppKit, one level down, so the eight
# direct deps of the probe were all present and the preflight passed 6/6. The
# only way a first-ring check catches a second-ring miss is if it stops being a
# first-ring check. So --closure walks the transitive closure: it opens every
# image the walk reaches, reads ITS dylib commands, and repeats until it stops
# finding new images. That is the same set dyld's dependency walk builds, and
# it is where the fault was.
#
# Re-export chains are included on purpose: LC_REEXPORT_DYLIB is one of the
# commands enumerated, so an image reachable only through a re-export is walked
# like any other. Excluding it would recreate the very gap this mode exists to
# close, one indirection deeper.
#
# WHERE THE CLOSURE IS CHECKED, AND WHY THERE ARE TWO ROOTS
#
# The walk is computed against the overlay, which is the authority on what
# exists. What dyld actually opens is whatever sits under DYLD_ROOT_PATH, and
# in this harness that root is the local staging cache, not the overlay: the
# overlay is mirrored onto local tmpfs to dodge virtiofs (BUS_OBJERR on
# page-fault reads of mmap'd files; FUSE_READLINK EIO leaking fuse_msgbuf until
# the guest OOMs). So the cache is a SUBSET, and a subset is where misses live.
#
# --staging-root therefore checks the closure a second time, against the
# directory the loader will really see, and a member that is in the closure but
# absent there is reported as IN-STAGING rather than MISSING. The two words
# mean different things and so point at different fixes: MISSING means the
# overlay is incomplete and the run cannot work at all; IN-STAGING means the
# overlay is fine and the staging list is short. The root run that reported
# "image not found" for Onyx2D was the second kind wearing the first kind's
# clothes.
#
# USAGE: python3 build-freebsd/check-guest-dylib-compat.py <binary> <overlay>
#        [--closure] [--staging-root DIR] [--emit-staging-trees]
#
# EXIT: 0 if every dependency resolves at a high enough version, 1 otherwise.

import collections
import os
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA
FAT_MAGIC_64 = 0xCAFEBABF
FAT_CIGAM_64 = 0xBFBAFECA
CPU_TYPE_X86_64 = 0x01000007
LC_LOAD_DYLIB = 0xC
LC_ID_DYLIB = 0xD
LC_REEXPORT_DYLIB = 0x8000001F
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_LAZY_LOAD_DYLIB = 0x20
LC_LOAD_UPWARD_DYLIB = 0x80000023
LC_RPATH = 0x8000001C
# Every command that names another image. LC_ID_DYLIB is the image's own name
# and LC_LOAD_UPWARD_DYLIB a system library, but both are edges of the closure
# for the purpose of "does this file have to be staged".
DYLIB_COMMANDS = (LC_LOAD_DYLIB, LC_ID_DYLIB, LC_REEXPORT_DYLIB,
                  LC_LOAD_WEAK_DYLIB, LC_LAZY_LOAD_DYLIB,
                  LC_LOAD_UPWARD_DYLIB)


def vstr(v):
    return "%d.%d.%d" % ((v >> 16) & 0xFFFF, (v >> 8) & 0xFF, v & 0xFF)


def thin_slice(d):
    """Return a 64-bit thin Mach-O view of `d`.

    Several guest libraries in the overlay are universal (libSystem.B.dylib,
    libobjc.A.dylib, libicucore.A.dylib all carry x86_64 and arm64). dyld
    picks the x86_64 slice, so that is the slice whose LC_ID_DYLIB matters;
    reading the fat header instead would just fail to parse.
    """
    magic = struct.unpack_from(">I", d, 0)[0]
    if magic in (FAT_MAGIC, FAT_CIGAM):
        nfat = struct.unpack_from(">I", d, 4)[0]
        is64 = magic == FAT_MAGIC_64
        for i in range(nfat):
            base = 8 + i * (32 if is64 else 20)
            cputype, _sub, offset = struct.unpack_from(">III", d, base)
            if cputype == CPU_TYPE_X86_64:
                return d[offset:]
        raise SystemExit("universal binary with no x86_64 slice")
    return d


def commands(d):
    """yield (cmd, offset) for each load command"""
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", d, 0)
    if magic not in (MH_MAGIC_64, MH_CIGAM_64):
        raise SystemExit("not a 64-bit thin Mach-O: magic %08x" % magic)
    if magic == MH_CIGAM_64:
        raise SystemExit("big-endian Mach-O, not expected here")
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, off)
        if cmdsize == 0:
            break
        yield cmd, off
        off += cmdsize


def _dylib_name_and_compat(d, off, cmdsize):
    """(name, compat version) from a dylib load command.

    There are two layouts and getting this wrong reads ASCII out of the
    version fields and prints nonsense like "needs 12141.101.116":

      dylib_command    (32-bit name offset)  name@8,  current@16, compat@20
      dylib_command_64 (64-bit name offset)  name@8,  current@20, compat@24

    They are told apart by whether the 4-byte name offset yields a path.
    """
    name_off = struct.unpack_from("<I", d, off + 8)[0]
    end = d.index(b"\0", off + name_off)
    name = d[off + name_off:end].decode("utf-8", "replace")
    if name.startswith("/"):
        cur, comp = struct.unpack_from("<II", d, off + 16)
        return name, comp
    name_off = struct.unpack_from("<Q", d, off + 8)[0]
    end = d.index(b"\0", off + name_off)
    name = d[off + name_off:end].decode("utf-8", "replace")
    cur, comp = struct.unpack_from("<II", d, off + 24)
    return name, comp


def load_commands(d):
    """(install name, required compat version) for every dylib command"""
    d = thin_slice(d)
    out = []
    for cmd, off in commands(d):
        if cmd not in DYLIB_COMMANDS:
            continue
        cmdsize = struct.unpack_from("<I", d, off + 4)[0]
        out.append(_dylib_name_and_compat(d, off, cmdsize))
    return out


def id_dylib_compat(path):
    d = thin_slice(open(path, "rb").read())
    for cmd, off in commands(d):
        if cmd == LC_ID_DYLIB:
            _name, comp = _dylib_name_and_compat(d, off, 0)
            return comp
    return None


# --- closure walk ---------------------------------------------------------

# @-prefix resolution. dyld tries these prefixes in a fixed order and a failure
# to resolve is a load failure, so they are handled explicitly rather than being
# string-concatenated onto a root: an "@loader_path" copied verbatim into a
# filesystem path is a file that does not exist, and a checker that reported it
# MISSING would be blaming the staging for a path the loader never builds.
RUNTIME_PREFIXES = ("@loader_path/", "@executable_path/", "@rpath/")


def rpaths(d):
    """LC_RPATH entries of one image, as written (may be relative)."""
    d = thin_slice(d)
    out = []
    for cmd, off in commands(d):
        if cmd != LC_RPATH:
            continue
        name_off = struct.unpack_from("<I", d, off + 8)[0]
        end = d.index(b"\0", off + name_off)
        out.append(d[off + name_off:end].decode("utf-8", "replace"))
    return out


def resolve(name, loader_dir, exe_dir, img_rpaths, root):
    """(guest path, host path) for a dylib name, or (name, None) if unresolvable.

    `guest path` is always absolute because that is the space the closure is
    kept in; the host path is that path under `root`, which may not exist (that
    is what --staging-root is for).
    """
    if name.startswith("/"):
        guest = os.path.normpath(name)
    elif name.startswith("@loader_path/"):
        guest = os.path.normpath(os.path.join(loader_dir, name[13:]))
    elif name.startswith("@executable_path/"):
        guest = os.path.normpath(os.path.join(exe_dir, name[16:]))
    elif name.startswith("@rpath/"):
        rest = name[7:]
        # An LC_RPATH is relative to the loading image, or absolute; both forms
        # occur and the difference decides where the file lands.
        for rp in img_rpaths:
            base = rp if rp.startswith("/") else os.path.join(loader_dir, rp)
            cand = os.path.normpath(os.path.join(base, rest))
            if os.path.exists(os.path.join(root, cand.lstrip("/"))):
                return cand, os.path.join(root, cand.lstrip("/"))
        return name, None
    else:
        return name, None
    return guest, os.path.join(root, guest.lstrip("/"))


def staging_tree(guest):
    """The overlay tree that must be staged for `guest` to be there.

    Deliberately COARSE: a tree, not a file. Staging exactly the closure's files
    would be tighter, but the current harness stages whole trees and the
    difference between "the closure" and "the trees the closure lands in" is
    paid for as extra bytes in a tmpfs copy -- while narrowing to per-file would
    be a behavioural change to the staging path that cannot be tested without a
    root run, and a miss here is exactly the failure this whole mechanism
    exists to prevent. Coarse is a superset, and a superset cannot regress the
    run that works today.
    """
    parts = guest.lstrip("/").split("/")
    if len(parts) >= 3 and parts[0] == "System" and parts[1] == "Library":
        return "/".join(parts[:3])
    if len(parts) >= 2 and parts[0] == "usr":
        return "/".join(parts[:2])
    return parts[0] if parts else ""


def walk_closure(binary, root, guest_name):
    """Breadth-first transitive closure of `binary` under `root`.

    Returns (closure, edges, unresolved, versions) where closure maps a guest
    path to the host file that backed it, edges is guest path -> [(referenced
    name, resolved guest path or None, required compat version)], versions is
    [(resolved guest path, required compat, found compat)] for every edge whose
    target was found, and unresolved is [(name, referenced from)].
    """
    exe_dir = "/" + os.path.dirname(guest_name) if "/" in guest_name else "/"
    if not exe_dir.endswith("/"):
        exe_dir += "/"

    closure = {}
    edges = collections.defaultdict(list)
    unresolved = []
    versions = []
    queue = collections.deque([(guest_name, binary, exe_dir, [])])
    while queue:
        guest, host, loader_dir, inherited = queue.popleft()
        if guest in closure:
            continue
        try:
            d = thin_slice(open(host, "rb").read())
        except (IOError, OSError, SystemExit) as exc:
            # An unreadable image is reported by the caller, not swallowed here:
            # a closure that quietly drops an image it could not parse is worse
            # than a loud failure, because it looks like a complete answer.
            unresolved.append((guest, "unreadable: %s" % exc))
            continue
        closure[guest] = host
        img_rpaths = rpaths(d) + inherited
        for name, need in load_commands(d):
            dep, dep_host = resolve(name, loader_dir, exe_dir, img_rpaths, root)
            edges[guest].append((name, dep, need))
            if dep is None or dep_host is None:
                unresolved.append((name, guest))
                continue
            if dep in closure:
                continue
            if not os.path.exists(dep_host):
                # Not an error yet: the caller distinguishes "the overlay does
                # not have it" from "the staging cache does not have it".
                continue
            versions.append((dep, need, id_dylib_compat(dep_host)))
            queue.append((dep, dep_host, os.path.dirname(dep) + "/", img_rpaths))
    return closure, edges, unresolved, versions


# --- reporting ------------------------------------------------------------

def report_closure(binary, overlay, staging_root, emit_trees, guest_name):
    closure, edges, unresolved, versions = walk_closure(binary, overlay, guest_name)

    print("binary:    %s" % binary)
    print("overlay:   %s" % overlay)
    print("closure:   %d image(s) reachable by transitive dylib walk" % len(closure))

    bad = 0
    for name, frm in unresolved:
        print("UNRESOLVED %-70s referenced from %s" % (name, frm))
        bad += 1

    # Every edge the walk could not find in the overlay. Distinguished from
    # IN-STAGING below because the two have different owners: a file the overlay
    # does not have cannot be staged at all, so no staging list can fix it.
    found = {p for p, _n, _v in versions}
    for guest in sorted(closure):
        for name, dep, _need in edges.get(guest, []):
            if dep is None or dep in found or os.path.exists(
                    os.path.join(overlay, dep.lstrip("/"))):
                continue
            print("MISSING   %-70s in the closure, not in the overlay" % dep)
            print("          required by %s; no staging list can supply it" % guest)
            bad += 1

    # The version rule the first ring applied to 8 deps, now applied to every
    # edge in the closure. dyld refuses a dylib whose compat version is lower
    # than the loader requires (dyld3/ClosureBuilder.cpp:372), and it says so in
    # a way that reads like a missing library.
    for guest, need, have in sorted(versions):
        if have is None:
            print("NOID      %-70s has no LC_ID_DYLIB to compare against" % guest)
            bad += 1
        elif have < need:
            print("OLD       %-70s needs %s, overlay has %s" % (guest, vstr(need), vstr(have)))
            print("          dyld would refuse this: 'found ... which has compat")
            print("          version (%s) which is less than required (%s)'"
                  % (vstr(have), vstr(need)))
            bad += 1

    if staging_root:
        print("staging:   %s" % staging_root)
        for guest in sorted(closure):
            if guest == guest_name:
                # Not staged: the harness copies the test binary to the cache
                # root by its own path and aborts loudly if that copy fails.
                # Judging the staging list on an image the staging list was
                # never meant to carry would make this check fail for a reason
                # it cannot fix, and a check that fails for the wrong reason
                # teaches its reader to ignore it.
                continue
            if not os.path.exists(os.path.join(staging_root, guest.lstrip("/"))):
                frm = edges.get(guest, [])
                print("IN-STAGING %-70s in the closure, not in the staging" % guest)
                print("           the overlay has it; the staging list does not")
                print("           it would reach the guest as: dyld: Library not loaded: %s" % guest)
                bad += 1

    if emit_trees:
        # The root image is excluded: it is not in the overlay, so it has no
        # tree there, and the harness copies it to the cache root by its own
        # path rather than by staging a directory. Leaving it in would either
        # emit a tree that cannot exist or, worse, degrade to "/" and stage the
        # entire overlay.
        derived = {staging_tree(g) for g in closure if g != guest_name}
        trees = sorted(t for t in derived if t)
        print("\nstaging trees derived from the closure (%d):" % len(trees))
        for t in trees:
            print("    %s" % t)
        print("\n# machine-readable, one tree per line:")
        for t in trees:
            print("#TREE\t%s" % t)

        # Self-check on the derivation itself. Every image the walk reached must
        # fall inside one of the trees just emitted, or staging those trees
        # would not have produced a usable root -- the whole failure this mode
        # exists to prevent, re-introduced one level up. It costs nothing and it
        # needs no staging cache, so it holds on a machine that has never run
        # the probe.
        uncov = sorted(g for g in closure
                       if g != guest_name and staging_tree(g) not in derived)
        for g in uncov:
            print("UNCOVERED %-70s falls in no derived staging tree" % g)
        if uncov:
            print("          the tree derivation does not cover this path shape;")
            print("          staging the derived list would not have staged it")
            bad += 1
        else:
            print("\ncoverage: every image in the closure falls in a derived tree")

    print("\n%s: %d problem(s)" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


def main():
    argv = sys.argv[1:]
    closure_mode = "--closure" in argv
    emit_trees = "--emit-staging-trees" in argv
    staging_root = None
    if "--staging-root" in argv:
        i = argv.index("--staging-root")
        if i + 1 >= len(argv):
            raise SystemExit("--staging-root needs a directory")
        staging_root = argv[i + 1]
        del argv[i:i + 2]
    for opt in ("--closure", "--emit-staging-trees"):
        if opt in argv:
            argv.remove(opt)
    if len(argv) != 2:
        raise SystemExit(__doc__)

    binary, overlay = argv[0], argv[1]

    if closure_mode:
        # The harness copies the test binary to the root of the staging cache
        # and runs it from there, so that -- not the host path -- is the guest
        # path the loader knows it by, and @executable_path resolves against it.
        guest_name = "/" + os.path.basename(binary)
        return report_closure(binary, overlay, staging_root, emit_trees, guest_name)

    want = load_commands(open(binary, "rb").read())

    print("binary:  %s" % binary)
    print("overlay: %s" % overlay)
    print("dependencies: %d\n" % len(want))

    bad = 0
    for name, need in sorted(want):
        if not name.startswith("/"):
            print("SKIP   %-72s not an absolute guest path" % name)
            bad += 1
            continue
        path = os.path.join(overlay, name.lstrip("/"))
        if not os.path.exists(path):
            print("MISSING %-72s not in the overlay" % name)
            bad += 1
            continue
        have = id_dylib_compat(path)
        if have is None:
            print("NOID   %-72s has no LC_ID_DYLIB to compare against" % name)
            bad += 1
            continue
        # dyld's rule, verbatim: refuse when the found version is lower.
        if have < need:
            print("OLD    %-72s needs %s, overlay has %s"
                  % (name, vstr(need), vstr(have)))
            print("       dyld would refuse this: 'found ... which has compat")
            print("       version (%s) which is less than required (%s)'"
                  % (vstr(have), vstr(need)))
            bad += 1
        else:
            note = "  <- 0xffffffff, printed as n/a by llvm-objdump" if have == 0xFFFFFFFF else ""
            print("ok     %-72s needs %-12s overlay has %s%s"
                  % (name, vstr(need), vstr(have), note))

    print("\n%s: %d problem(s)" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
