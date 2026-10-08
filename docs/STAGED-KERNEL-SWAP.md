# STAGED-KERNEL-SWAP - the rebuilt x86_64 libsystem_kernel in the staged overlay

Control #142 step 6. The x86_64 libsystem_kernel.dylib.x86_64 linked cleanly
(rc=0, 678556 B, nm gate passed); this plants it into the staged overlay the same
way milestone 01 (Control #25, libsystem_malloc) did, and runs the standard dlopen
probe (Control #59-2 / #60 recipe).

## Planting (Control #25 route: copy the artifact into the overlay)
  cp "$DARLING_BUILD_DIR/src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib.x86_64" \
     "$DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib"
(the pre-existing fat kernel was backed up to /tmp/kernel-fat-backup.dylib and
restored afterwards).

## (0) Planted file, geometry, UUID
  $ file .../libsystem_kernel.dylib
  Mach-O 64-bit x86_64 dynamically linked shared library, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|WEAK_DEFINES|BINDS_TO_WEAK|NO_REEXPORTED_DYLIBS>
  $ llvm-lipo -info .../libsystem_kernel.dylib
  Non-fat file: .../libsystem_kernel.dylib is architecture: x86_64
  $ llvm-otool -l .../libsystem_kernel.dylib | grep -A2 LC_UUID
    uuid E172B900-59DC-36AE-92BE-22DF5FB63A93
The lx artifact carries the SAME uuid E172B900-59DC-36AE-92BE-22DF5FB63A93.

## (1) Probe run - new log name (60-2 style)
Log: /tmp/foundation-probe-157-kernel-swap.log  (1292537 lines, 102451256 B)
Recipe (Control #59-2): DARLING_TEST_BINARY=cft-fwmacho-probe-macho,
DARLING_STAGING_TREES from /tmp/foundation-probe-58-1.log line 14 + QuartzCore,
then `sudo env <DYLD_PRINT_*> "$DARLING_BUILD_DIR"/launch-dynamic`.

The rebuilt kernel is loaded:
  dyld: loaded: <E172B900-59DC-36AE-92BE-22DF5FB63A93> /usr/lib/system/libsystem_kernel.dylib

The os_unfair_lock abort (libsystem_platform+0x8237 via stub 0x64d020) is GONE:
  grep -cE 'os_unfair_lock.*abort|0x64d020|0x8237|SIGABRT|signal 6|signal 11' -> 0
The log also has 0 "Library not loaded" and 0 "Symbol not found". The guest now
runs PAST every earlier wall (CoreGraphics, IOKit) and reaches the Chrome
framework initializer, where it stops (the probe hung; killed by pid, no crash):
  ... dyld: calling initializer function 0x376a6d3214c0 in /Frameworks/Google
      Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome
      for Testing Framework
So the next failure class after the unlock-lane abort is a hang in the Chrome
framework initializer, not the os_unfair_lock abort.

## Verdict (one line)
The rebuilt x86_64 kernel loads with its own UUID and the os_unfair_lock abort no
longer reproduces; the run advances to the Chrome framework initializer and hangs
there.

## Repro
  export DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR
  cp "$DARLING_BUILD_DIR/src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib.x86_64" "$DARLING_OVERLAY/usr/lib/system/libsystem_kernel.dylib"
  export DARLING_TEST_BINARY=cft-fwmacho-probe-macho
  export BASE58=$(sed -n '14p' /tmp/foundation-probe-58-1.log | sed 's/^staging trees: derived from the closure -- //')
  export DARLING_STAGING_TREES="$BASE58:System/Library/Frameworks/QuartzCore.framework"
  timeout 180 sudo env DARLING_SRC_DIR=$DARLING_SRC_DIR DARLING_OVERLAY=$DARLING_OVERLAY DARLING_BUILD_DIR=$DARLING_BUILD_DIR DARLING_TEST_BINARY=$DARLING_TEST_BINARY DARLING_STAGING_TREES=$DARLING_STAGING_TREES DYLD_BIND_AT_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_BINDINGS=1 DYLD_PRINT_WEAK_BINDINGS=1 DYLD_PRINT_APIS=1 DYLD_PRINT_INTERPOSING=1 DYLD_PRINT_SEGMENTS=1 DYLD_PRINT_STATISTICS=1 DYLD_PRINT_STATISTICS_DETAILS=1 DYLD_PRINT_RPATHS=1 DYLD_PRINT_WARNINGS=1 DYLD_PRINT_INITIALIZERS=1 DYLD_PRINT_DOFS=1 DYLD_PRINT_OPTS=1 DYLD_PRINT_ENV=1 DYLD_PRINT_CODE_SIGNATURES=1 DYLD_PRINT_REBASINGS=1 DYLD_PRINT_TO_STDERR=1 "$DARLING_BUILD_DIR"/launch-dynamic > /tmp/foundation-probe-157-kernel-swap.log 2>&1
  grep -m1 'loaded:.*libsystem_kernel' /tmp/foundation-probe-157-kernel-swap.log
