#!/usr/bin/env python3
"""objc-narrow.py - turn `ninja -t commands <libobjc target>` into a narrow x86_64
rebuild of libobjc.A.dylib: the objc4 runtime objects only (written under WORK/obj)
and one link whose firstpass dylib inputs are redirected to the installed overlay
dylibs (same install names).

usage: objc-narrow.py ALL_CMDS WORK OVERLAY   -> writes WORK/compile.sh, WORK/link.sh
"""
import os, re, shlex, sys

all_cmds, work, overlay = sys.argv[1:4]
target = "src/external/objc4/runtime/libobjc.A.dylib"
lines = open(all_cmds).read().split("\n")
comp, link = [], None
for ln in lines:
    if " -c " in ln and "/objc4/runtime/" in ln:
        comp.append(ln)
    elif ("-o " + target + " ") in ln and "-dylib_install_name" in ln:
        link = ln
assert comp and link, (len(comp), bool(link))

os.makedirs(os.path.join(work, "obj"), exist_ok=True)
objs = []
out = ["#!/bin/sh", "set -e", "cd \"$DARLING_BUILD_DIR\""]
n = 0
for ln in comp:
    m = re.search(r" -o (\S+\.o) -c (\S+)\s*$", ln)
    obj_rel, srcf = m.group(1), m.group(2)
    # keep the cmake object layout under WORK/obj
    objname = obj_rel.split("objc_obj.dir/", 1)[1].replace("/", "__")
    obj = os.path.join(work, "obj", objname)
    c = ln[:m.start()]
    c = c.replace("-arch i386 -arch x86_64", "-arch x86_64")
    c = re.sub(r" -MD -MT \S+ -MF \S+", "", c)
    # the installed libobjc has LOCKDEBUG on (objc-os.h: LOCKDEBUG == !NDEBUG), the
    # tree's Release flags define NDEBUG; drop it so the exports and lock checks match
    c = c.replace(" -DNDEBUG", "").replace("-DOBJC_IS_DEBUG_BUILD=0", "-DOBJC_IS_DEBUG_BUILD=1")
    c = re.sub(r"^\s*", "", c)
    out.append("%s -o %s -c %s &" % (c, obj, srcf))
    n += 1
    if n % 8 == 0:
        out.append("wait")
    objs.append((obj_rel, obj))
out.append("wait")
out.append("echo compiled %d objects" % n)
open(os.path.join(work, "compile.sh"), "w").write("\n".join(out) + "\n")

l = link.strip()
l = re.sub(r"-arch i386\s+", "", l)
l = re.sub(r"^: && ", "", l)
l = re.sub(r" && :\s*$", "", l)
for obj_rel, obj in objs:
    l = l.replace(" " + obj_rel + " ", " " + obj + " ")
# redirect every firstpass / build-tree dylib to the installed one with the same install name
pairs = re.findall(r"-Wl,-dylib_file,(/usr/lib/[^: ]+):(\S+)", l)
for inst, build in pairs:
    l = l.replace("-Wl,-dylib_file,%s:%s" % (inst, build), "-Wl,-dylib_file,%s:%s%s" % (inst, overlay, inst))
    l = l.replace(" " + build + " ", " " + overlay + inst + " ")
# inputs given as relative build paths (not in -dylib_file pairs): map by file name
byname = {
 "libc++.1.dylib": "/usr/lib/libc++.1.dylib",
 "libc++abi.dylib": "/usr/lib/libc++abi.dylib",
 "libSystem.B.dylib": "/usr/lib/libSystem.B.dylib",
 "libsystem_c_firstpass.dylib": "/usr/lib/system/libsystem_c.dylib",
 "libsystem_dyld_firstpass.dylib": "/usr/lib/system/libdyld.dylib",
 "libsystem_blocks_firstpass.dylib": "/usr/lib/system/libsystem_blocks.dylib",
 "libunwind_firstpass.dylib": "/usr/lib/system/libunwind.dylib",
 "libsystem_m_firstpass.dylib": "/usr/lib/system/libsystem_m.dylib",
 "libmacho_firstpass.dylib": "/usr/lib/system/libmacho.dylib",
 "libplatform_firstpass.dylib": "/usr/lib/system/libsystem_platform.dylib",
 "libsystem_kernel_firstpass.dylib": "/usr/lib/system/libsystem_kernel.dylib",
 "libsystem_pthread_firstpass.dylib": "/usr/lib/system/libsystem_pthread.dylib",
 "libcompiler_rt_firstpass.dylib": "/usr/lib/system/libcompiler_rt.dylib",
 "liblibdispatch_shared_firstpass.dylib": "/usr/lib/system/libdispatch.dylib",
 "libsystem_malloc_firstpass.dylib": "/usr/lib/system/libsystem_malloc.dylib",
}
def fix(m):
    p = m.group(1)
    if os.path.basename(p) not in byname:
        return m.group(0)
    return " " + overlay + byname[os.path.basename(p)]
l = re.sub(r" (src/\S+\.dylib)(?=\s|$)", fix, l)
ld = os.environ.get("LD64", "")
if ld:
    l = re.sub(r"-fuse-ld=\S+", "-fuse-ld=" + ld, l)
l = re.sub(r"-o " + re.escape(target), "-o " + os.path.join(work, "libobjc.A.dylib.x86_64"), l)
open(os.path.join(work, "link.sh"), "w").write("#!/bin/sh\nset -e\ncd \"$DARLING_BUILD_DIR\"\n" + l + "\n")
print("objects: %d; remaining build-tree dylib inputs: %s" % (n, re.findall(r"\s(src/\S+\.dylib)", l)))
