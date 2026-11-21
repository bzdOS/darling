#!/bin/sh
# keep-one-check.sh - "one tree, one final build, nothing grows" check. Read-only by default; only --clean-sockets (stale mldr sockets) and --apply (exactly the --list paths) delete.
# usage: keep-one-check.sh           prints exactly one line: KEEP-ONE verdict=pass|fail <details>; exit 1 on fail
#        keep-one-check.sh --clean-sockets  removes only stale /tmp/darling-mldr-<pid>-<fd> sockets (dead pid, >5 min); prints KEEP-ONE sockets removed=N kept_live=M
#        keep-one-check.sh --list    prints the candidate paths (one per line); still deletes nothing
#        keep-one-check.sh --apply   deletes exactly the paths --list prints, then prints the verdict again. Only paths whose
#                                    realpath is inside <root>/build or <root>/backup; any other path: refuse, delete nothing, rc 4.
#                                    Skipped (rc 3, "KEEP-ONE apply skipped: ...") while <root>/build/.darling-run.lock is held
#                                    or a darlingserver/mldr process is alive. rc 2 on bad usage.
# Env: KEEPONE_ROOT (default: parent of DARLING_BUILD_DIR; one of the two is required)  KEEPONE_MAX_GB (default 14)
#      KEEPONE_ALLOW  extra build/ dir names (space separated globs) that are not experiment dirs
#      KEEPONE_ALLOW_FILES  extra top-level build/ file globs never candidates; a glob matching suspend-<kind>-<stamp>.log or .dump
#                           (e.g. suspend-cv-1117-133903.*) protects that pair and it does not count toward the limit below
#      KEEPONE_SUSPEND_KEEP (default 3)  newest complete log+dump pairs kept per suspend-(rwlock|mutex|cv) kind; older ones are candidates
# Checks:
#  (series files under 1 KB are aborted starts and are not counted as series)
#  1. build/ top-level *.log *.png *.core core.* chrome-sr-* series-*.out *-series.out only from the latest two
#     series (series = a series-<p>.out or <p>-series.out file, newest first by mtime; a chrome-sr-* file belongs
#     to the series with the longest matching prefix <p>; other logs/pngs/cores count as stale).
#  2. backup/ holds at most one entry per library (name with .pre-* and .dylib removed); older ones are candidates.
#  3. build/ top-level dirs are all keep-list/allowlist; anything else is an experiment dir (candidate).
#  4. total size of KEEPONE_ROOT under KEEPONE_MAX_GB.
#  Refinements: files modified within KEEPONE_MIN_AGE_MIN minutes (default 120), files whose name contains
#  KEEPONE_FOREIGN (default alt2-r, another owner's series) and files open in a live process (fstat) are never candidates.
# Never flagged: build/dserver/*, last-two-series files, /tmp/rs-* (outside the scanned dirs), and the dir keep-list
# below (sources not in git, dirs scripts write to).
ROOT="${KEEPONE_ROOT:-}"; [ -z "$ROOT" ] && [ -n "${DARLING_BUILD_DIR:-}" ] && ROOT=$(dirname "$DARLING_BUILD_DIR")
[ -n "$ROOT" ] || { echo "KEEP-ONE verdict=fail no-root"; exit 2; }
B="$ROOT/build"; BK="$ROOT/backup"; MAXG="${KEEPONE_MAX_GB:-14}"
FOREIGN="${KEEPONE_FOREIGN:-alt2-r}"; AGE="${KEEPONE_MIN_AGE_MIN:-120}"
# flags combine in any order: --clean-sockets runs first, then --list / --apply / the check
# (only $1 used to be read, so "--apply --clean-sockets" silently skipped the socket cleanup)
MODE=check; CLEAN=0
for _a in "$@"; do
	case "$_a" in
	--list) MODE=list;; --apply) MODE=apply;; --clean-sockets) CLEAN=1;;
	*) echo "KEEP-ONE verdict=fail unknown-arg $_a"; exit 2;;
	esac
done
SKEEP="${KEEPONE_SUSPEND_KEEP:-3}"
# --apply runs itself once under the run lock (lockf -t 0 as the series scripts do); lockf rc 73/75 = lock not available
if [ $MODE = apply ] && [ -z "$KEEPONE_LOCKED" ]; then
	[ -d "$B" ] || { echo "KEEP-ONE apply skipped: no $B"; exit 3; }
	KEEPONE_LOCKED=1 lockf -t 0 "$B/.darling-run.lock" sh "$0" "$@"; _rc=$?
	case $_rc in 73|75) echo "KEEP-ONE apply skipped: $B/.darling-run.lock is held (a series is running)"; exit 3;; esac
	exit $_rc
fi
# stale mldr RPC sockets: /tmp/darling-mldr-<pid>-<fd>, a socket, pid dead, nobody holds it (fstat on the path lists no process),
# older than 5 min (every condition re-checked per entry)
stale_sock() { # $1 = path
	[ -S "$1" ] || return 1
	_b=${1##*/}; _x=${_b#darling-mldr-}; _pid=${_x%%-*}
	case "$_pid" in ''|*[!0-9]*) return 1;; esac
	kill -0 "$_pid" 2>/dev/null && return 1
	[ -n "$(find "$1" -mmin +5 2>/dev/null)" ] || return 1
	# held by a live process (a running darlingserver, a guest of another pid): not stale; fstat failing counts as held
	_h=$(sudo -n fstat "$1" 2>/dev/null) || return 1
	[ -z "$(printf '%s\n' "$_h" | sed 1d)" ]
}
if [ $CLEAN = 1 ]; then
	rm_n=0; live=0
	for _f in /tmp/darling-mldr-[0-9]*-[0-9]*; do
		[ -S "$_f" ] || continue
		if stale_sock "$_f"; then { rm -f "$_f" 2>/dev/null || sudo -n rm -f "$_f" 2>/dev/null; } && [ ! -e "$_f" ] && rm_n=$((rm_n+1)); else live=$((live+1)); fi
	done
	echo "KEEP-ONE sockets removed=$rm_n kept_live=$live"
	[ $# -eq 1 ] && exit 0
fi
ns=0; for _f in /tmp/darling-mldr-[0-9]*-[0-9]*; do stale_sock "$_f" && ns=$((ns+1)); done

# dirs holding sources that are not in git (cc.sh, link.sh, *.c, *.m, *.s, headers)
KEEP_SRC="dyld-raw *-relink* lsk-* me-lsk marker-suppl suppl-stubs* supl-bind* un-bind atomic-* extras-noobjc objc-suppl wl-debug-src"
# dirs the scripts write to
KEEP_OUT="dserver real-macho crash-probe coredata-coreservices wayland-window-test wayland-backend quartzcore stub-data cores src"
# build infrastructure and per-script scratch/output dirs (recreated by their scripts)
KEEP_INFRA="CMakeFiles Developer -I i386 x86_64 x86_64-apple-darwin20 dtape cg-supl suppl wl-debug-copy wlr-protocols-gen constdict-desc got-slot-undoc msgSend-imp-undoc bundle-principal-class-test sc-stubs extras-objc-classes opendirectory-stub bootstrap-shim bootstrap-probe cfinit const-objc window-draw-probe screens objc-only tz-check *-probe-test scratch bitmap-check bitmap-draw-check nscgcolor-check kqueue-close-probe font-metrics-probe throw-trace appkit-only"
# work dirs of the current probes and of everything the gate (stage-gate.sh, probes/*.sh) rebuilds or uses
KEEP_PROBE="imageio imageio-probe suspend-rwlock-probe suspend-mutex-probe suspend-cv-probe suspend-wake-probe* window-draw* netsegv nullsrc unwind-resume v5store catx-kvc-probe eintr-prepost-probe psynch-ring-probe ptattach-probe semtw-probe port-construct-probe font-families-probe font-resolve-probe syscall482-probe throw-trace-selftest"
KEEP_DIRS="$KEEP_SRC $KEEP_OUT $KEEP_INFRA $KEEP_PROBE $KEEPONE_ALLOW"
# top-level build/ files (KEEP_FILES) and backup/ entries (KEEP_BACKUPS) that are never candidates: the [keepone] lists of
# darling.toml, read as data from the generated $B/keepone.env, or straight from the toml (gen-from-schema.py --emit keepone)
# when it is not generated; neither readable: fail, never an empty keep list. KEEPONE_ALLOW_FILES/_BACKUPS add to them.
# (suspend-(rwlock|mutex|cv)-* pairs are limited to the newest KEEPONE_SUSPEND_KEEP per kind, see section 1b)
_HERE=$(dirname "$0")
if [ -f "$B/keepone.env" ]; then _kol=$(cat "$B/keepone.env")
else _kol=$(python3 "$_HERE/gen-from-schema.py" --emit keepone 2>/dev/null); fi
_kf=$(printf '%s\n' "$_kol" | sed -n "s/^KEEPONE_FILES='\(.*\)'$/\1/p")
_kb=$(printf '%s\n' "$_kol" | sed -n "s/^KEEPONE_BACKUPS='\(.*\)'$/\1/p")
{ [ -n "$_kf" ] && [ -n "$_kb" ]; } || { echo "KEEP-ONE verdict=fail no-keepone-list"; exit 2; }
KEEP_FILES="$_kf $KEEPONE_ALLOW_FILES"
KEEP_BACKUPS="$_kb $KEEPONE_ALLOW_BACKUPS"

T=$(mktemp -t keepone.XXXXXX) || exit 2
trap 'rm -f "$T"' EXIT INT TERM

# ---- series, newest first ----
SER=""
if [ -d "$B" ]; then
	for f in $(cd "$B" && ls -t series-*.out *-series.out 2>/dev/null); do
		[ "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" -ge 1024 ] || continue	# under 1 KB: an aborted start, not a series
		case "$f" in series-*.out) p=${f#series-}; p=${p%.out};; *) p=${f%-series.out};; esac
		case " $SER " in *" $p "*) ;; *) SER="$SER $p";; esac
	done
fi
set -- $SER; KEEP1=$1; KEEP2=$2

# series that a chrome-sr-* file belongs to: longest prefix p such that the rest starts with a digit or -r<digit>
owner() {
	_n=${1#chrome-sr-}; _best=""
	for _p in $SER; do
		case "$_n" in
		"$_p"[0-9]*|"$_p"-r[0-9]*) [ ${#_p} -gt ${#_best} ] && _best=$_p;;
		esac
		# a series name with a trailing letter suffix (fix23c) also owns the logs named <base><N> (fix231)
		_b=${_p%[a-z]}
		if [ "$_b" != "$_p" ] && [ -n "$_b" ]; then
			case "$_n" in
			"$_b"[0-9]*|"$_b"-r[0-9]*) [ ${#_p} -gt ${#_best} ] && _best=$_p;;
			esac
		fi
	done
	printf '%s' "$_best"
}

# inodes open in a live process on the build filesystem
OPEN=" $(timeout 60 fstat -f "$B" 2>/dev/null | awk 'NR>1 && $6 ~ /^[0-9]+$/ {print $6}' | sort -u | tr '\n' ' ')"

# probe runner logs and traces (run-*-probe.sh): <kind>-MMDD-HHMMSS.log and <kind>-MMDD-HHMMSS-ktrace.out; handled in 1d only
is_probe() { printf '%s\n' "$1" | grep -Eq '^[a-z][a-z0-9-]*-[0-9]{4}-[0-9]{6}(\.log|-ktrace\.out)$'; }
# Old sig-reentry-probe-run.sh naming (before <kind>-MMDD-HHMMSS): <prefix><run>-<i>.log and
# <prefix><run>-<i>-dserver[-head].txt, for the runner prefixes used on this tree. All of them are probe candidates.
PROBE_OLD_PREFIXES="cancelrpc abold abnew cnt sigre"
is_probe_old() { for _p in $PROBE_OLD_PREFIXES; do printf '%s\n' "$1" | grep -Eq "^${_p}[0-9]*-[0-9]+(\.log|-dserver(-head)?\.txt)\$" && return 0; done; return 1; }

# ---- 1. stale top-level logs/png/cores ----
if [ -d "$B" ]; then
	for f in $(cd "$B" && ls -p | grep -v / | grep -E '\.(log|png|core)$|^core\.|^chrome-sr-|^series-.*\.out$|-series\.out$|-series-out\.txt$'); do
		case "$f" in
		series-*.out) o=${f#series-}; o=${o%.out};;
		*-series.out) o=${f%-series.out};;
		chrome-sr-*) o=$(owner "$f");;
		*) o="";;
		esac
		case "$f" in suspend-*) continue;; esac	# suspend-(rwlock|mutex|cv) handled in 1b, other suspend-* (wake probe) protected
		is_probe "$f" && continue	# probe runner logs: section 1d
		is_probe_old "$f" && continue	# old-format probe runner logs: section 1d
		[ -n "$FOREIGN" ] && case "$f" in *"$FOREIGN"*) continue;; esac
		_k=0; set -f; for g in $KEEP_FILES; do case "$f" in $g) _k=1; break;; esac; done; set +f; [ $_k = 1 ] && continue
		[ -n "$(find "$B/$f" -mmin -"$AGE" 2>/dev/null)" ] && continue
		case "$OPEN" in *" $(stat -f %i "$B/$f") "*) continue;; esac
		if [ -n "$o" ] && { [ "$o" = "$KEEP1" ] || [ "$o" = "$KEEP2" ]; }; then continue; fi
		printf '%s\tlog\t%s\n' "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" "$B/$f"
	done >>"$T"
fi

# ---- 1b. suspend-(rwlock|mutex|cv) log+dump pairs: keep the newest $SKEEP complete pairs per kind (by stamp in the name) ----
if [ -d "$B" ]; then
	for k in rwlock mutex cv; do
		_n=0
		for st in $(cd "$B" && ls -p | grep -E "^suspend-$k-[0-9]+-[0-9]+\.(log|dump)\$" | sed -E 's/\.(log|dump)$//' | sort -ur); do
			_k=0; set -f; for g in $KEEP_FILES; do case "$st.log" in $g) _k=1;; esac; case "$st.dump" in $g) _k=1;; esac; done; set +f
			[ $_k = 1 ] && continue
			if [ -f "$B/$st.log" ] && [ -f "$B/$st.dump" ]; then _n=$((_n+1)); [ $_n -le "$SKEEP" ] && continue; fi
			for e in log dump; do
				f=$st.$e; [ -f "$B/$f" ] || continue
				[ -n "$(find "$B/$f" -mmin -"$AGE" 2>/dev/null)" ] && continue
				case "$OPEN" in *" $(stat -f %i "$B/$f") "*) continue;; esac
				printf '%s\tlog\t%s\n' "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" "$B/$f"
			done
		done
	done >>"$T"
fi

# ---- 1c. ktrace traces (DARLING_KTRACE runs): <name>-ktrace.out, only the newest one is kept; older ones are stale logs
# (counted in stale_logs and shown by --list; KEEPONE_ALLOW_FILES does not protect them) ----
if [ -d "$B" ]; then
	_n=0
	for f in $(cd "$B" && ls -t -p | grep -v / | grep -E -- '-ktrace\.out$'); do
		is_probe "$f" && continue	# probe traces: section 1d, they never push a series trace out
		_n=$((_n+1)); [ $_n -le 1 ] && continue
		[ -n "$(find "$B/$f" -mmin -"$AGE" 2>/dev/null)" ] && continue
		case "$OPEN" in *" $(stat -f %i "$B/$f") "*) continue;; esac
		printf '%s\tlog\t%s\n' "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" "$B/$f"
	done >>"$T"
fi

# ---- 1d. probe runner logs/traces: the newest one of each kind (<kind>.log, <kind>-ktrace.out) is kept; older ones are
# type probe: shown by --list, removed by --apply, counted as probe_logs, never fail the verdict (the gate's own probes write them) ----
if [ -d "$B" ]; then
	_seen=""
	for f in $(cd "$B" && ls -t -p | grep -v /); do
		if is_probe_old "$f"; then
			case "$OPEN" in *" $(stat -f %i "$B/$f") "*) continue;; esac
			printf '%s\tprobe\t%s\n' "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" "$B/$f"
			continue
		fi
		is_probe "$f" || continue
		case "$f" in *-ktrace.out) _kd=${f%-[0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]-ktrace.out}.ktrace;;
		*) _kd=${f%-[0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9].log}.log;; esac
		case " $_seen " in *" $_kd "*) ;; *) _seen="$_seen $_kd"; continue;; esac
		case "$OPEN" in *" $(stat -f %i "$B/$f") "*) continue;; esac
		printf '%s\tprobe\t%s\n' "$(stat -f %z "$B/$f" 2>/dev/null || echo 0)" "$B/$f"
	done >>"$T"
fi

# ---- 3. experiment dirs ----
if [ -d "$B" ]; then
	for d in $(cd "$B" && ls -p -A | grep / | tr -d /); do
		keep=0
		for g in $KEEP_DIRS; do
			case "$d" in $g) keep=1; break;; esac
		done
		[ $keep = 1 ] && continue
		printf '%s\tdir\t%s\n' "$(( $(timeout 60 du -sk "$B/./$d" 2>/dev/null | cut -f1) * 1024 ))" "$B/$d"
	done >>"$T"
fi

# ---- 2. backup/: one entry per library, newest kept ----
if [ -d "$BK" ]; then
	for f in $(cd "$BK" && ls -t); do
		l=${f%%.pre-*}; l=${l%.dylib}
		_k=0; set -f; for g in $KEEP_BACKUPS; do case "$f" in $g) _k=1; break;; esac; done; set +f; [ $_k = 1 ] && { SEEN="$SEEN $l"; continue; }
		case " $SEEN " in *" $l "*) printf '%s\tbackup\t%s\n' "$(( $(timeout 60 du -sk "$BK/$f" 2>/dev/null | cut -f1) * 1024 ))" "$BK/$f" >>"$T";; *) SEEN="$SEEN $l";; esac
	done
fi

if [ "$MODE" = list ]; then cut -f3 "$T"; exit 0; fi

# ---- --apply: delete exactly what --list prints, nothing outside <root>/build and <root>/backup ----
if [ "$MODE" = apply ]; then
	for _p in darlingserver mldr; do
		pgrep -x "$_p" >/dev/null 2>&1 && { echo "KEEP-ONE apply skipped: live process $_p"; exit 3; }
	done
	RB=$(realpath "$B" 2>/dev/null); RK=$(realpath "$BK" 2>/dev/null)
	bad=0
	while IFS="$(printf '\t')" read -r _sz _ty _path; do
		_rp=$(realpath "$_path" 2>/dev/null)
		case "$_rp" in
		""|"$RB"|"$RK") bad=$((bad+1)); echo "KEEP-ONE apply refused: $_path (realpath '${_rp}')" >&2;;
		"$RB"/*|"$RK"/*) ;;
		*) bad=$((bad+1)); echo "KEEP-ONE apply refused: $_path resolves to $_rp, outside $RB and $RK" >&2;;
		esac
	done <"$T"
	if [ $bad -gt 0 ]; then echo "KEEP-ONE apply refused: $bad path(s) outside allowed prefixes, nothing deleted"; exit 4; fi
	fail=0; n=0
	while IFS="$(printf '\t')" read -r _sz _ty _path; do
		rm -rf "$_path" 2>/dev/null || sudo -n rm -rf "$_path" 2>/dev/null
		if [ -e "$_path" ] || [ -L "$_path" ]; then fail=$((fail+1)); echo "KEEP-ONE apply failed: $_path" >&2; else n=$((n+1)); echo "removed $_path"; fi
	done <"$T"
	echo "KEEP-ONE apply removed=$n failed=$fail"
	KEEPONE_LOCKED= sh "$0" || true
	[ $fail = 0 ]; exit $?
fi

# ---- 4. total size ----
kb=$(timeout 300 du -sk "$ROOT" 2>/dev/null | cut -f1); kb=${kb:-0}
over=0; [ "$kb" -gt $((MAXG*1024*1024)) ] && over=1
nl=$(grep -c "	log	" "$T"); nd=$(grep -c "	dir	" "$T"); nb=$(grep -c "	backup	" "$T")
np=$(grep -c "	probe	" "$T")
cb=$(awk -F'\t' '$2!="probe" {s+=$1} END{print s+0}' "$T")
gb=$(awk -v k="$kb" 'BEGIN{printf "%.1f", k/1048576}')
v=pass; { [ "$ns" -gt 0 ] || [ "$nl" -gt 0 ] || [ "$nd" -gt 0 ] || [ "$nb" -gt 0 ] || [ $over = 1 ]; } && v=fail
echo "KEEP-ONE verdict=$v series=${KEEP1:-none},${KEEP2:-none} stale_logs=$nl experiment_dirs=$nd backup_dups=$nb stale_sockets=$ns candidate_bytes=$cb total=${gb}G max=${MAXG}G probe_logs=$np"
[ $v = pass ]
