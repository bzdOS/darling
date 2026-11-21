#!/bin/sh
# Compare the per-library bind distribution (bind + lazy-bind + weak-bind) of two Mach-O images.
# Guards against the ld64.lld 19 reexport bug: with -reexport_library it writes both LC_LOAD_DYLIB and
# LC_REEXPORT_DYLIB for one library but counts one ordinal, so libraries listed after it get shifted bind
# ordinals (symbols bound to the wrong library). A relink that only adds a reexport must keep the distribution.
# Usage: bind-dist-check.sh <old-image> <new-image>
# Exit 0 when identical, 1 when they differ (both distributions printed), 2 on usage error.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <old-image> <new-image>" >&2; exit 2; }
for f in "$1" "$2"; do [ -f "$f" ] || { echo "FATAL: $f missing" >&2; exit 2; }; done

dist() {
	llvm-objdump --macho --bind --lazy-bind --weak-bind "$1" 2>/dev/null |
		awk 'NF > 3 && $1 ~ /^__/ { n[$(NF-1)]++ } END { for (k in n) print k, n[k] }' | sort
}
old=$(dist "$1")
new=$(dist "$2")
if [ "${old}" = "${new}" ]; then
	echo "BIND-DIST same: $(echo "${new}" | tr '\n' ' ')"
	exit 0
fi
echo "BIND-DIST differ"
echo "old: $(echo "${old}" | tr '\n' ' ')"
echo "new: $(echo "${new}" | tr '\n' ' ')"
exit 1
