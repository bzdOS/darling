#!/bin/sh
# run-bundle-principal-class.sh — run the bundle micro-probe, without Wayland.
#
# Usage: sh build-freebsd/run-bundle-principal-class.sh
#   DRY_RUN=1 sh build-freebsd/run-bundle-principal-class.sh   # needs no root
#
# Env: DARLING_SRC_DIR, DARLING_BUILD_DIR, DARLING_OVERLAY (same as the other
# build-freebsd scripts), DRY_RUN=1.
#
# What this is: the window probe's fourth run stopped at
#
#     [step 03] FATAL: bundle has no NSPrincipalClass
#
# for a backend whose Info.plist does name NSPrincipalClass. This runs
# tests/src/bundle-principal-class.m instead, which asks the three questions
# -[NSBundle principalClass] asks (NSBundle.m:744-761) and prints what the guest
# answers for each, so the wall is located rather than guessed at.
#
# Why NO_SEAT=1 is not a shortcut: principalClass is answered entirely inside
# Foundation, before anything calls wl_display_connect. Requiring a live sway
# socket here would make the run depend on a compositor this program never
# talks to, and a failure would then be ambiguous between "the answer is wrong"
# and "the seat moved". Everything that could actually invalidate the answer
# still runs: the vendored-backend hash check, the dylib closure walk, the
# staging list derived from it, the probe-shape check, the harness rebuild
# gate, and the one root run itself.
#
# What it costs: one root run, same as the window probe's. The log is its own
# file, outside the source tree, so it can never be picked up by git add -A.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec env \
	NO_SEAT=1 \
	TEST_BIN=bundle-principal-class-macho \
	TEST_BUILD_SH=build-bundle-principal-class-test.sh \
	LOG="${DARLING_BUILD_DIR:-/var/darling-build}/bundle-principal-class.log" \
	sh "${SCRIPT_DIR}/run-wayland-window-probe.sh"
