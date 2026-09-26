#!/bin/sh
# Auto-discovers, inits, and flattens every submodule needed to resolve
# the vendored macOS SDK's symlinks, then packages the result into
# tests/vendor/macosx-sdk-flat.tar.gz.
#
# Why this exists: the vendored Developer/.../MacOSX.sdk tree (and several
# submodules' own header trees, e.g. corefoundation's root-level .h files)
# are almost entirely symlinks pointing back into other src/external/*
# submodules — most of them not initialized by default. readlink() on the
# dev VM's virtiofs mount is broken (see tests/vendor/README.md), so these
# must be resolved ONCE on the host (where symlinks work fine) and shipped
# as regular files, never re-derived from the guest.
#
# Before this script, that was a fully manual loop: try a build on the VM,
# read one "file not found" error, `readlink` it on the host to find which
# submodule it needs, `git submodule update --init` that one submodule,
# re-flatten, re-transfer, retry — one header at a time, sometimes dozens of
# rounds for a single large component (Foundation needed ~15 submodules).
# This script does the whole discovery + init + flatten loop in one pass:
#   1. Scan every symlink under the given root dirs; for each DANGLING one,
#      extract which src/external/<name> (or nested submodule) it points at.
#   2. `git submodule update --init` all of those at once.
#   3. Repeat until a pass finds nothing new to init (nested submodules —
#      e.g. corefoundation's own submodules/swift-corelibs-foundation — need
#      a second pass once their parent is checked out).
#   4. Flatten every given root with `tar -h` (dereference), which silently
#      drops any symlink whose target STILL doesn't exist (e.g. i386/ARM/
#      sparc architecture headers with no submodule at all) rather than
#      aborting — exactly the behavior wanted here.
#
# Usage: sh build-freebsd/sync-flat-sdk.sh
# Run on the HOST (not the dev VM) — it needs real readlink(), which is
# exactly what the dev VM's virtiofs mount cannot do.
#
# Environment:
#   DARLING_SRC_DIR — root of this repository (default: directory of this script/..)
#   DARLING_SSH_KEY, DARLING_SSH_DEST — only used for the hint printed at the end
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${DARLING_SRC_DIR:-${SCRIPT_DIR}}"
SDK="${SRC}/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

# Frameworks actually referenced by something this repo currently builds.
# Deliberately NOT "every *.framework under the SDK" — that tree also holds
# WebCore, JavaScriptCore, dozens of others with no bearing on anything
# built here, and scanning them for dangling symlinks triggers `git
# submodule update --init` on their (often huge: ruby, python, openjdk)
# backing repos for no reason. Add a name here only once something actually
# needs it (a "file not found" in a framework not yet listed).
FRAMEWORKS="CoreFoundation Security"

# usr/include exposes header symlinks into language-runtime and other large
# submodules that have nothing to do with what this repo currently builds
# (python2.7, ruby's headers, openjdk's JNI headers, ...). Discovery would
# otherwise `git submodule update --init` these the first time ANY file in
# usr/include happens to be scanned — confirmed live: a first attempt at
# this script cloned python/ruby/openjdk/WebCore-adjacent repos before this
# list existed. Extend it if a legitimately new large irrelevant one shows
# up; do NOT just delete this mechanism to "fix" a slow run.
SKIP_SUBMODULES="python ruby perl5 openjdk swift WebCore JavaScriptCore WTF cocotron mono tcl bind9"

# Extra header-only roots beyond the SDK tree itself, flattened into their
# own named subdirectory of the output (used via a matching -I on the
# compile side — see build-freebsd/build-real-macho-tests.sh).
EXTRA_ROOTS="src/external/corefoundation:corefoundation-headers"

FLAT="${SRC}/tests/vendor/.sdk-flat-work"
rm -rf "${FLAT}"
mkdir -p "${FLAT}/usr/include"

# --- discovery + init loop ---
discover_needed_submodules() {
	# Prints one candidate submodule path (relative to $SRC, "src/external/
	# <name>") per DANGLING symlink target under $1, deduplicated.
	#
	# Resolving the target purely in the shell (cd into dirname(target),
	# pwd) silently breaks for exactly the case that matters here: when the
	# submodule truly isn't checked out yet, dirname(target) doesn't exist,
	# `cd` fails, and the whole entry gets dropped instead of recognized as
	# "needed" — the opposite of what discovery is for. os.path.normpath
	# needs no existing directories to resolve a relative path correctly.
	python3 - "$1" "${SRC}" <<'PYEOF'
import os, sys
root, src = sys.argv[1], sys.argv[2]
seen = set()
for dirpath, dirnames, filenames in os.walk(root):
	for name in dirnames + filenames:
		p = os.path.join(dirpath, name)
		if not os.path.islink(p):
			continue
		target = os.readlink(p)
		abs_target = target if os.path.isabs(target) else os.path.normpath(os.path.join(os.path.dirname(p), target))
		if os.path.exists(abs_target):
			continue
		rel = os.path.relpath(abs_target, src)
		if rel.startswith("src/external/"):
			parts = rel.split(os.sep)
			cand = os.sep.join(parts[:3])
			if cand not in seen:
				seen.add(cand)
				print(cand)
PYEOF
}

pass=1
while :; do
	echo "=== submodule discovery pass ${pass} ==="
	needed="$(discover_needed_submodules "${SDK}/usr/include"
		for fw in ${FRAMEWORKS}; do
			discover_needed_submodules "${SDK}/System/Library/Frameworks/${fw}.framework"
		done
		for r in ${EXTRA_ROOTS}; do
			discover_needed_submodules "${SRC}/${r%%:*}"
		done)"
	to_init=""
	skipped=""
	for path in $(printf '%s\n' "${needed}" | sort -u); do
		name="${path##*/}"
		is_skipped=0
		for s in ${SKIP_SUBMODULES}; do
			[ "${name}" = "${s}" ] && is_skipped=1 && break
		done
		if [ "${is_skipped}" = 1 ]; then
			skipped="${skipped} ${path}"
			continue
		fi
		state="$(git -C "${SRC}" submodule status "${path}" 2>/dev/null | cut -c1)"
		[ "${state}" = "-" ] && to_init="${to_init} ${path}"
	done
	[ -n "${skipped}" ] && echo "skipped (in SKIP_SUBMODULES):${skipped}"
	to_init="$(printf '%s' "${to_init}" | xargs -n1 2>/dev/null | sort -u | xargs)"
	if [ -z "${to_init}" ]; then
		echo "nothing left to init"
		break
	fi
	echo "initializing:${to_init}"
	# shellcheck disable=SC2086
	git -C "${SRC}" submodule update --init ${to_init}
	pass=$((pass + 1))
	[ "${pass}" -gt 6 ] && { echo "too many passes, stopping"; break; }
done

# corefoundation carries its own nested submodule (swift-corelibs-foundation,
# where CFBase.h/CFString.h/etc actually live) — not reachable by the loop
# above since it's a submodule OF a submodule, not of the top-level repo.
if [ -d "${SRC}/src/external/corefoundation/.git" ]; then
	git -C "${SRC}/src/external/corefoundation" submodule update --init \
		submodules/swift-corelibs-foundation 2>/dev/null || true
fi

# --- flatten ---
echo "=== flattening ==="
(cd "${SDK}/usr/include" && tar -chf - --warning=no-file-changed . 2>/dev/null) \
	| tar -xf - -C "${FLAT}/usr/include"

for fw in ${FRAMEWORKS}; do
	headers="${SDK}/System/Library/Frameworks/${fw}.framework/Versions/A/Headers"
	[ -d "${headers}" ] || continue
	mkdir -p "${FLAT}/Frameworks/${fw}.framework/Headers"
	(cd "${headers}" && tar -chf - --warning=no-file-changed . 2>/dev/null) \
		| tar -xf - -C "${FLAT}/Frameworks/${fw}.framework/Headers"
done

for r in ${EXTRA_ROOTS}; do
	src_dir="${SRC}/${r%%:*}"
	out_dir="${FLAT}/${r##*:}"
	mkdir -p "${out_dir}"
	(cd "${src_dir}" && find . -maxdepth 1 -iname "*.h" -print0 \
		| tar -chf - --warning=no-file-changed --null -T - 2>/dev/null) \
		| tar -xf - -C "${out_dir}"
done

tar czf "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" -C "${FLAT}" .
rm -rf "${FLAT}"
echo "=== done: $(du -h "${SRC}/tests/vendor/macosx-sdk-flat.tar.gz" | cut -f1) ==="
# Transfer destination is machine-specific; keep it out of the tree.
#   DARLING_SSH_KEY  — ssh identity for the dev VM
#   DARLING_SSH_DEST — user@host of the dev VM
KEY="${DARLING_SSH_KEY:-<ssh-key>}"
DEST="${DARLING_SSH_DEST:-<user>@<dev-vm>}"
echo "Transfer to the dev VM with (set DARLING_SSH_KEY and DARLING_SSH_DEST):"
echo "  scp -i ${KEY} tests/vendor/macosx-sdk-flat.tar.gz ${DEST}:/tmp/sdk-flat.tar.gz"
echo "  ssh -i ${KEY} ${DEST} 'rm -rf /tmp/sdk-flat && mkdir -p /tmp/sdk-flat && tar xzf /tmp/sdk-flat.tar.gz -C /tmp/sdk-flat'"
