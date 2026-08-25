#!/bin/sh
# Build duct-tape (XNU Mach IPC) for FreeBSD 15.1.
# Usage:
#   su -m root -c 'sh build-freebsd/build-dtape.sh 2>&1 | tee /tmp/dtape-build.log'
#
# Steps:
#   1. Build mig (Mach Interface Generator) from bootstrap_cmds
#   2. Generate MIG stubs matching CMakeLists.txt add_library() exactly
#   3. Compile all duct-tape source files, report errors per file
#
# Requires: pkg install cmake bison flex llvm
#
# Environment:
#   DARLING_BUILD_DIR  — where build artefacts go (default: /tmp/darling-build)
#   DARLING_SRC_DIR    — root of this repository  (default: directory of this script/..)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${DARLING_SRC_DIR:-${SCRIPT_DIR}}/src"
DS=${ROOT}/external/darlingserver
DT=${DS}/duct-tape
BOOT=${ROOT}/external/bootstrap_cmds
COMPAT=${ROOT}/startup/compat-includes
BUILD="${DARLING_BUILD_DIR:-/var/darling-build}/dtape"

mkdir -p "${BUILD}"

MIGBUILD=${BUILD}/mig-build
MIGSCRIPT=${MIGBUILD}/build-mig
DTBUILD=${BUILD}/dtape-build

# ─── Step 1: Build mig ────────────────────────────────────────────────────────
# Build migcom manually (not via cmake) to avoid dangling macOS SDK symlinks
# in bootstrap_cmds/darling/include/. Use duct-tape xnu/osfmk for <mach/...>.
echo "=== Step 1: Building mig ==="
mkdir -p "${MIGBUILD}" && cd "${MIGBUILD}"

MIGCOMSRC="${BOOT}/migcom.tproj"
DARLING_SRC="${BOOT}/darling/src"
# Minimal mach stub headers for migcom host build (no macOS SDK, no duct-tape deep includes)
MACH_INC="${BOOT}/freebsd-mig-includes"

MIGDEFS="-Du_int=unsigned -D__int64_t=int64_t -D__int32_t=int32_t \
  -D__darwin_natural_t=unsigned -D__uint32_t=uint32_t \
  -D__uint16_t=uint16_t -D__uint64_t=uint64_t \
  -D_BSD_I386__TYPES_H_ \
  -DDARLING -DDARLING_FREEBSD \
  -DMIG_VERSION_UNUSED"

# Create fake arch/_types.h so duct-tape's mach/i386/vm_types.h is satisfied.
# migcom only needs the types at parse time; we just need the include to work.
mkdir -p "${MIGBUILD}/i386"
cat > "${MIGBUILD}/i386/_types.h" << 'EOFH'
#pragma once
/* Minimal Apple arch/_types.h stub for migcom host build on FreeBSD */
#include <stdint.h>
typedef unsigned int      __darwin_natural_t;
typedef int               __darwin_ct_rune_t;
typedef unsigned char     __darwin_uint8_t;
typedef unsigned short    __darwin_uint16_t;
typedef unsigned int      __darwin_uint32_t;
typedef unsigned long long __darwin_uint64_t;
typedef signed char       __darwin_int8_t;
typedef short             __darwin_int16_t;
typedef int               __darwin_int32_t;
typedef long long         __darwin_int64_t;
typedef long              __darwin_ssize_t;
typedef unsigned long     __darwin_size_t;
EOFH
mkdir -p "${MIGBUILD}/arm"
cp "${MIGBUILD}/i386/_types.h" "${MIGBUILD}/arm/_types.h"

MIGCFLAGS="-std=c99 -Wno-dangling-else -Wno-implicit-function-declaration \
  -include ${MACH_INC}/mig_version.h \
  -I${MIGCOMSRC} -I${MACH_INC} -I${MIGBUILD} ${MIGDEFS}"

# Run bison on parser.y
bison -d -o "${MIGBUILD}/parser.c" "${MIGCOMSRC}/parser.y"
# Run flex on lexxer.l
flex -o "${MIGBUILD}/lexer.c" "${MIGCOMSRC}/lexxer.l"

# Compile migcom (handler.c and xtracemig.c excluded — cmake had handler.c commented out)
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/error.c"     -o "${MIGBUILD}/error.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/global.c"    -o "${MIGBUILD}/global.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/header.c"    -o "${MIGBUILD}/header.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/mig.c"       -o "${MIGBUILD}/mig.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/routine.c"   -o "${MIGBUILD}/routine.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/server.c"    -o "${MIGBUILD}/server.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/statement.c" -o "${MIGBUILD}/statement.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/string.c"    -o "${MIGBUILD}/string.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/type.c"      -o "${MIGBUILD}/type.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/user.c"      -o "${MIGBUILD}/user.o"
clang ${MIGCFLAGS} -c "${MIGCOMSRC}/utils.c"     -o "${MIGBUILD}/utils.o"
clang ${MIGCFLAGS} -c "${DARLING_SRC}/xtracemig.c" -o "${MIGBUILD}/xtracemig.o"
clang ${MIGCFLAGS} -c "${MIGBUILD}/parser.c"     -o "${MIGBUILD}/parser.o"
clang ${MIGCFLAGS} -c "${MIGBUILD}/lexer.c"      -o "${MIGBUILD}/lexer.o"

# Link migcom
clang -o "${MIGBUILD}/migcom" \
  "${MIGBUILD}/error.o" "${MIGBUILD}/global.o" "${MIGBUILD}/header.o" \
  "${MIGBUILD}/mig.o" "${MIGBUILD}/routine.o" "${MIGBUILD}/server.o" \
  "${MIGBUILD}/statement.o" "${MIGBUILD}/string.o" "${MIGBUILD}/type.o" \
  "${MIGBUILD}/user.o" "${MIGBUILD}/utils.o" \
  "${MIGBUILD}/xtracemig.o" \
  "${MIGBUILD}/parser.o" "${MIGBUILD}/lexer.o"

echo "migcom: $(${MIGBUILD}/migcom --version 2>&1 || echo 'built')"

# Generate build-mig shell wrapper (what cmake's migexe target would produce)
# build-mig wraps migcom: adds CC and correct invocation
awk -v "migcc=clang" \
    -v "migcom=${MIGBUILD}/migcom" \
    -f "${BOOT}/darling/src/mig.awk" \
    "${MIGCOMSRC}/mig.sh" > "${MIGSCRIPT}"
# FreeBSD: bash is at /usr/local/bin/bash, not /bin/bash
sed -i '' 's|#!/bin/bash|#!/usr/local/bin/bash|' "${MIGSCRIPT}"
chmod 0755 "${MIGSCRIPT}"
echo "mig wrapper: ${MIGSCRIPT}"
ls -la "${MIGSCRIPT}" || { echo "ERROR: mig script not built"; exit 1; }

# ─── Step 2: Generate MIG stubs ───────────────────────────────────────────────
echo ""
echo "=== Step 2: Generating MIG stubs ==="

MACHD="${DTBUILD}/xnu/osfmk/mach"
DEVD="${DTBUILD}/xnu/osfmk/device"
UNDD="${DTBUILD}/xnu/osfmk/UserNotification"
mkdir -p "${MACHD}" "${DEVD}" "${UNDD}"

MIGCOM="${MIGBUILD}/migcom"
# Include paths for clang -E preprocessing of .defs files
# KERNEL_USER/KERNEL_SERVER needed so multiline subsystem{#if KERNEL_USER...#endif} works
CPP_MIGFLAGS="-I${DT}/defines -I${DT}/xnu/osfmk -I${DT}/xnu/bsd -I${DT}/xnu -I${DT}/xnu/EXTERNAL_HEADERS \
  -D__MACH30__ -DKERNEL_USER=1 -DKERNEL_SERVER=1 \
  -DKERNEL_PRIVATE -DMACH_KERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DPRIVATE \
  -P"

# run_mig: preprocess .defs with clang -E then pipe to migcom
# Usage: run_mig <defs> <outdir> <basename> <server_suffix> <user_suffix>
#   server_suffix: "_server" or "Server"
#   user_suffix:   "_user"   or "User"
run_mig() {
    DEFS="$1"; OUTDIR="$2"; BASE="$3"; SSUF="$4"; USUF="$5"
    SRCDIR=$(dirname "${DEFS}")
    ( echo "#line 1 \"${DEFS}\""; cat "${DEFS}" ) | \
    clang -E -x c ${CPP_MIGFLAGS} -I"${SRCDIR}" - 2>/dev/null | \
    "${MIGCOM}" \
        -server  "${OUTDIR}/${BASE}${SSUF}.c" \
        -user    "${OUTDIR}/${BASE}${USUF}.c" \
        -header  "${OUTDIR}/${BASE}.h" \
        -sheader "${OUTDIR}/${BASE}_server.h" \
        -xtracemig /dev/null \
    2>&1 && echo "  OK: ${BASE}" || echo "  WARN: mig failed for ${BASE}"
}

# UNDReply uses CamelCase suffix (matches CMakeLists: UNDReplyServer.c)
run_mig "${DT}/xnu/osfmk/UserNotification/UNDReply.defs" "${UNDD}" "UNDReply" "Server" "User"

# All other .defs use underscore suffix (_server.c, _user.c)
for d in \
    "${DT}/xnu/osfmk/device/device.defs:${DEVD}:device" \
    "${DT}/xnu/osfmk/mach/audit_triggers.defs:${MACHD}:audit_triggers" \
    "${DT}/xnu/osfmk/mach/clock.defs:${MACHD}:clock" \
    "${DT}/xnu/osfmk/mach/clock_priv.defs:${MACHD}:clock_priv" \
    "${DT}/xnu/osfmk/mach/clock_reply.defs:${MACHD}:clock_reply" \
    "${DT}/xnu/osfmk/mach/exc.defs:${MACHD}:exc" \
    "${DT}/xnu/osfmk/mach/host_notify_reply.defs:${MACHD}:host_notify_reply" \
    "${DT}/xnu/osfmk/mach/host_priv.defs:${MACHD}:host_priv" \
    "${DT}/xnu/osfmk/mach/host_security.defs:${MACHD}:host_security" \
    "${DT}/xnu/osfmk/mach/lock_set.defs:${MACHD}:lock_set" \
    "${DT}/xnu/osfmk/mach/mach_eventlink.defs:${MACHD}:mach_eventlink" \
    "${DT}/xnu/osfmk/mach/mach_exc.defs:${MACHD}:mach_exc" \
    "${DT}/xnu/osfmk/mach/mach_host.defs:${MACHD}:mach_host" \
    "${DT}/xnu/osfmk/mach/mach_notify.defs:${MACHD}:mach_notify" \
    "${DT}/xnu/osfmk/mach/mach_port.defs:${MACHD}:mach_port" \
    "${DT}/xnu/osfmk/mach/mach_vm.defs:${MACHD}:mach_vm" \
    "${DT}/xnu/osfmk/mach/mach_voucher.defs:${MACHD}:mach_voucher" \
    "${DT}/xnu/osfmk/mach/mach_voucher_attr_control.defs:${MACHD}:mach_voucher_attr_control" \
    "${DT}/xnu/osfmk/mach/memory_entry.defs:${MACHD}:memory_entry" \
    "${DT}/xnu/osfmk/mach/memory_object.defs:${MACHD}:memory_object" \
    "${DT}/xnu/osfmk/mach/memory_object_control.defs:${MACHD}:memory_object_control" \
    "${DT}/xnu/osfmk/mach/memory_object_default.defs:${MACHD}:memory_object_default" \
    "${DT}/xnu/osfmk/mach/notify.defs:${MACHD}:notify" \
    "${DT}/xnu/osfmk/mach/processor.defs:${MACHD}:processor" \
    "${DT}/xnu/osfmk/mach/processor_set.defs:${MACHD}:processor_set" \
    "${DT}/xnu/osfmk/mach/resource_notify.defs:${MACHD}:resource_notify" \
    "${DT}/xnu/osfmk/mach/restartable.defs:${MACHD}:restartable" \
    "${DT}/xnu/osfmk/mach/task.defs:${MACHD}:task" \
    "${DT}/xnu/osfmk/mach/task_access.defs:${MACHD}:task_access" \
    "${DT}/xnu/osfmk/mach/thread_act.defs:${MACHD}:thread_act" \
    "${DT}/xnu/osfmk/mach/upl.defs:${MACHD}:upl" \
    "${DT}/xnu/osfmk/mach/vm32_map.defs:${MACHD}:vm32_map" \
    "${DT}/xnu/osfmk/mach/vm_map.defs:${MACHD}:vm_map" \
; do
    DEFS="${d%%:*}"; REST="${d#*:}"; OUTDIR="${REST%%:*}"; BASE="${REST##*:}"
    run_mig "${DEFS}" "${OUTDIR}" "${BASE}" "_server" "_user"
done

echo ""
echo "Generated MIG .c files: $(find ${DTBUILD}/xnu -name '*.c' | wc -l)"

# ─── Step 3: Compile duct-tape ────────────────────────────────────────────────
echo ""
echo "=== Step 3: Compiling duct-tape ==="
set +e   # compile loop handles errors individually
mkdir -p "${DTBUILD}/objs"

# rtsig.h (normally generated by rtsig binary)
printf '#define LINUX_SIGRTMIN %d\n#define LINUX_SIGRTMAX %d\n' 65 126 \
    > "${DTBUILD}/rtsig.h"

# Source list matching CMakeLists.txt add_library() exactly
# duct-tape/src/
DT_SRCS="\
  ${DT}/src/init.c \
  ${DT}/src/misc.c \
  ${DT}/src/stubs.c \
  ${DT}/src/locks.c \
  ${DT}/src/memory.c \
  ${DT}/src/task.c \
  ${DT}/src/thread.c \
  ${DT}/src/timer.c \
  ${DT}/src/traps.c \
  ${DT}/src/host.c \
  ${DT}/src/processor.c \
  ${DT}/src/kqchan.c \
  ${DT}/src/semaphore.c \
  ${DT}/src/psynch.c \
  ${DT}/src/condvar.c \
  ${DT}/src/debug.c \
  ${DT}/src/kernelrpc_stubs.c \
  ${DT}/src/freebsd_percpu.s"

# xnu/libkern
DT_SRCS="${DT_SRCS} \
  ${DT}/xnu/libkern/os/refcnt.c \
  ${DT}/xnu/libkern/gen/OSAtomicOperations.c \
  ${DT}/xnu/libkern/c++/priority_queue.cpp"

# xnu/osfmk/kern
DT_SRCS="${DT_SRCS} \
  ${DT}/xnu/osfmk/kern/ipc_clock.c \
  ${DT}/xnu/osfmk/kern/ipc_host.c \
  ${DT}/xnu/osfmk/kern/ipc_kobject.c \
  ${DT}/xnu/osfmk/kern/ipc_mig.c \
  ${DT}/xnu/osfmk/kern/ipc_misc.c \
  ${DT}/xnu/osfmk/kern/ipc_sync.c \
  ${DT}/xnu/osfmk/kern/ipc_tt.c \
  ${DT}/xnu/osfmk/kern/turnstile.c \
  ${DT}/xnu/osfmk/kern/waitq.c \
  ${DT}/xnu/osfmk/kern/clock.c \
  ${DT}/xnu/osfmk/kern/ltable.c \
  ${DT}/xnu/osfmk/kern/mpsc_queue.c \
  ${DT}/xnu/osfmk/kern/thread_call.c \
  ${DT}/xnu/osfmk/kern/mk_timer.c \
  ${DT}/xnu/osfmk/kern/host.c \
  ${DT}/xnu/osfmk/kern/host_notify.c \
  ${DT}/xnu/osfmk/kern/timer_call.c \
  ${DT}/xnu/osfmk/kern/clock_oldops.c \
  ${DT}/xnu/osfmk/kern/sync_sema.c \
  ${DT}/xnu/osfmk/kern/sync_lock.c \
  ${DT}/xnu/osfmk/kern/syscall_emulation.c \
  ${DT}/xnu/osfmk/kern/ux_handler.c \
  ${DT}/xnu/osfmk/kern/exception.c \
  ${DT}/xnu/osfmk/kern/task_ident.c"

# xnu/osfmk/ipc
DT_SRCS="${DT_SRCS} \
  ${DT}/xnu/osfmk/ipc/ipc_entry.c \
  ${DT}/xnu/osfmk/ipc/ipc_hash.c \
  ${DT}/xnu/osfmk/ipc/ipc_importance.c \
  ${DT}/xnu/osfmk/ipc/ipc_init.c \
  ${DT}/xnu/osfmk/ipc/ipc_kmsg.c \
  ${DT}/xnu/osfmk/ipc/ipc_mqueue.c \
  ${DT}/xnu/osfmk/ipc/ipc_notify.c \
  ${DT}/xnu/osfmk/ipc/ipc_object.c \
  ${DT}/xnu/osfmk/ipc/ipc_port.c \
  ${DT}/xnu/osfmk/ipc/ipc_pset.c \
  ${DT}/xnu/osfmk/ipc/ipc_right.c \
  ${DT}/xnu/osfmk/ipc/ipc_space.c \
  ${DT}/xnu/osfmk/ipc/ipc_table.c \
  ${DT}/xnu/osfmk/ipc/ipc_voucher.c \
  ${DT}/xnu/osfmk/ipc/mach_debug.c \
  ${DT}/xnu/osfmk/ipc/mach_kernelrpc.c \
  ${DT}/xnu/osfmk/ipc/mach_msg.c \
  ${DT}/xnu/osfmk/ipc/mach_port.c \
  ${DT}/xnu/osfmk/ipc/mig_log.c \
  ${DT}/xnu/osfmk/ipc/ipc_eventlink.c"

# Architecture-specific rtclock
ARCH=$(uname -m)
if [ "${ARCH}" = "amd64" ] || [ "${ARCH}" = "x86_64" ]; then
    DT_SRCS="${DT_SRCS} ${DT}/xnu/osfmk/i386/rtclock.c"
elif [ "${ARCH}" = "aarch64" ] || [ "${ARCH}" = "arm64" ]; then
    if [ -f "${DT}/xnu/osfmk/arm/rtclock.c" ]; then
        DT_SRCS="${DT_SRCS} ${DT}/xnu/osfmk/arm/rtclock.c"
    fi
fi

# prng + vm32
DT_SRCS="${DT_SRCS} \
  ${DT}/xnu/osfmk/prng/prng_random.c \
  ${DT}/xnu/osfmk/vm/vm32_user.c"

# MIG-generated server stubs (matching CMakeLists.txt add_library list)
DT_SRCS="${DT_SRCS} \
  ${MACHD}/clock_priv_server.c \
  ${MACHD}/clock_reply_user.c \
  ${MACHD}/clock_server.c \
  ${MACHD}/exc_user.c \
  ${MACHD}/exc_server.c \
  ${MACHD}/host_priv_server.c \
  ${MACHD}/host_security_server.c \
  ${MACHD}/lock_set_server.c \
  ${MACHD}/mach_exc_server.c \
  ${MACHD}/mach_exc_user.c \
  ${MACHD}/mach_host_server.c \
  ${MACHD}/mach_port_server.c \
  ${MACHD}/mach_vm_server.c \
  ${MACHD}/mach_voucher_attr_control_server.c \
  ${MACHD}/mach_voucher_server.c \
  ${MACHD}/memory_entry_server.c \
  ${MACHD}/notify_user.c \
  ${MACHD}/processor_server.c \
  ${MACHD}/processor_set_server.c \
  ${MACHD}/restartable_server.c \
  ${MACHD}/task_server.c \
  ${MACHD}/thread_act_server.c \
  ${MACHD}/mach_eventlink_server.c \
  ${MACHD}/vm32_map_server.c \
  ${DEVD}/device_server.c \
  ${UNDD}/UNDReplyServer.c"

# pthread/kern_synch.c needs its own -I for pthread headers
DT_PTHREAD_SRCS="${DT}/pthread/kern_synch.c"

# ── Compile flags matching duct-tape CMakeLists.txt ───────────────────────────
DTFLAGS="\
  -std=gnu11 -ggdb -O0 -fblocks -ffunction-sections -fdata-sections \
  -Wno-incompatible-library-redeclaration \
  -Wno-error=int-conversion \
  -Wno-unused-variable \
  -Wno-unused-function \
  -Wno-parentheses \
  -I${COMPAT} \
  -I${DT}/defines \
  -I${DT}/xnu/osfmk \
  -I${DT}/xnu/bsd \
  -I${DT}/xnu/libkern \
  -I${DT}/xnu/osfmk/libsa \
  -I${DT}/xnu/pexpert \
  -I${DT}/xnu/iokit \
  -I${DT}/xnu/EXTERNAL_HEADERS \
  -I${DT}/xnu \
  -I${DT}/include \
  -I${DT}/internal-include \
  -I${DS}/include \
  -I${DS}/internal-include \
  -I${DS}/generated-rpc/include \
  -I${DS}/generated-rpc/internal-include \
  -I${ROOT}/libsimple/include \
  -I${DTBUILD} \
  -I${DTBUILD}/xnu/osfmk \
  -D__DARLING__ -DDARLING_DEBUG -DPAGE_SIZE_FIXED \
  -DCONFIG_SCHED_TRADITIONAL -DCONFIG_SCHED_TIMESHARE_CORE \
  -DAPPLE -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DPRIVATE \
  -D__MACHO__=1 -Dvolatile=__volatile -DNEXT -D__LITTLE_ENDIAN__=1 \
  -D__private_extern__=extern -D_NLINK_T -DVM32_SUPPORT=1 \
  -DMACH_KERNEL_PRIVATE -DARCH_PRIVATE -DDRIVER_PRIVATE \
  -D_KERNEL_BUILD -DKERNEL_BUILD -DMACH_KERNEL \
  -DBSD_BUILD -DBSD_KERNEL_PRIVATE -DLP64KERN=1 -DLP64_DEBUG=0 \
  -DTIMEZONE=0 -DPST=0 -DQUOTA -DABSOLUTETIME_SCALAR_TYPE \
  -DCONFIG_LCTX -DMACH -DCONFIG_ZLEAKS -DNO_DIRECT_RPC \
  -DPSYNCH -DSECURE_KERNEL -DOLD_SEMWAIT_SIGNAL \
  -DIPFIREWALL_FORWARD -DIPFIREWALL_DEFAULT_TO_ACCEPT -DTRAFFIC_MGT \
  -DRANDOM_IP_ID -DTCP_DROP_SYNFIN -DICMP_BANDLIM \
  -DIFNET_INPUT_SANITY_CHK \
  -DCONFIG_MBUF_JUMBO -DCONFIG_WORKQUEUE \
  -DCONFIG_TASK_MAX=512 -DCONFIG_IPC_TABLE_ENTRIES_STEPS=256 \
  -DNAMEDSTREAMS -DCONFIG_VOLFS -DCONFIG_IMGSRC_ACCESS \
  -DCONFIG_TRIGGERS -DCONFIG_VFS_FUNNEL -DCONFIG_EXT_RESOLVER \
  -DCONFIG_SEARCHFS -DIPSEC -DIPSEC_ESP \
  -DCONFIG_KN_HASHSIZE=64 -DCONFIG_THREAD_MAX=1024 \
  -DCONFIG_MSG_BSIZE=4096 -DCONFIG_MEMORYSTATUS -DCONFIG_JETSAM \
  -D_CLOCK_T=1 -DNO_KDEBUG=1 -DCONFIG_IPC_KERNEL_MAP_SIZE=64 \
  -D__DARWIN_LITTLE_ENDIAN=1234 \
  -D__DARWIN_BYTE_ORDER=__DARWIN_LITTLE_ENDIAN \
  -DCC_USING_FENTRY=1 -DIMPORTANCE_INHERITANCE=1 \
  -DKERNEL_SERVER=1 -DKERNEL_USER=1 \
  -DMACH_NOTIFY_SEND_POSSIBLE_EXPECTED \
  -DXNU_TARGET_OS_OSX=1 \
  -DDARLING_FREEBSD \
  -DLIBSIMPLE_FREEBSD=1 \
  -DDSERVER_SINGLE_THREADED=1"

# C++ flags for .cpp files
DTCXXFLAGS="${DTFLAGS} -std=c++17"

# ── Compile loop ──────────────────────────────────────────────────────────────
ERRS=0
PASS=0

compile_one() {
    SRC="$1"
    CFLAGS="$2"
    OBJ="${DTBUILD}/objs/$(echo "${SRC}" | tr '/' '_' | tr ' ' '_').o"
    OUT=$(clang ${CFLAGS} -c "${SRC}" -o "${OBJ}" 2>&1)
    EC=$?
    if [ ${EC} -ne 0 ]; then
        echo "FAIL: $(basename ${SRC})"
        echo "${OUT}" | grep 'error:' | head -5 || true
        ERRS=$((ERRS+1))
    else
        PASS=$((PASS+1))
    fi
}

for src in ${DT_SRCS}; do
    [ -f "${src}" ] || { echo "MISSING: ${src}"; continue; }
    case "${src}" in
        *.cpp) compile_one "${src}" "${DTCXXFLAGS}" ;;
        *.s|*.S) compile_one "${src}" "" ;;
        # stubs.c declares 'extern FILE* stdout;' without <stdio.h>; on FreeBSD the
        # actual symbol is __stdoutp — redirect at preprocessor level.
        */stubs.c) compile_one "${src}" "${DTFLAGS} -Dstdout=__stdoutp" ;;
        *)     compile_one "${src}" "${DTFLAGS}" ;;
    esac
done

# freebsd_compat.c: userspace shims for Linux glibc functions (get_nprocs etc.)
# Must be compiled WITHOUT -DKERNEL so POSIX headers (<unistd.h>) are accessible.
COMPAT_FLAGS="\
  -std=gnu11 -ggdb -O0 \
  -I${COMPAT} \
  -I${DT}/include \
  -I${DS}/include \
  -DDARLING_FREEBSD"
compile_one "${DT}/src/freebsd_compat.c" "${COMPAT_FLAGS}"

# kern_synch.c: use pre-staged headers to avoid 9p EMSGSIZE on pthread/kern symlinks.
# pthread-staged/ is committed to the repo to avoid broken symlinks.
PTHREAD_STAGED="${SCRIPT_DIR}/build-freebsd/pthread-staged"

OBJ="${DTBUILD}/objs/kern_synch.o"
# Compile staged kern_synch.c so "kern/..." includes resolve from PTHREAD_STAGED
OUT=$(clang ${DTFLAGS} -I${PTHREAD_STAGED} -c "${PTHREAD_STAGED}/kern_synch.c" -o "${OBJ}" 2>&1)
if [ $? -ne 0 ]; then
    echo "FAIL: kern_synch.c"
    echo "${OUT}" | grep 'error:' | head -5 || true
    ERRS=$((ERRS+1))
else
    PASS=$((PASS+1))
fi

echo ""
echo "=== SUMMARY: ${PASS} compiled OK, ${ERRS} failed ==="

if [ ${ERRS} -eq 0 ]; then
    echo ""
    echo "=== Step 4: Linking libdtape.a ==="
    ar rcs "${DTBUILD}/libdtape.a" "${DTBUILD}/objs"/*.o
    echo "Built: ${DTBUILD}/libdtape.a ($(du -sh ${DTBUILD}/libdtape.a | cut -f1))"
fi
