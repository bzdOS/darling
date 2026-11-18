#!/bin/sh
# keep-one-check.sh - "one tree, one final build, nothing grows" check. READ-ONLY: never deletes anything.
# usage: keep-one-check.sh           prints exactly one line: KEEP-ONE verdict=pass|fail <details>; exit 1 on fail
#        keep-one-check.sh --clean-sockets  removes only stale /tmp/darling-mldr-<pid>-<fd> sockets (dead pid, >30 min); prints KEEP-ONE sockets removed=N kept_live=M
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
ROOT="${KEEPONE_ROOT:-$(dirname "${DARLING_BUILD_DIR:?set DARLING_BUILD_DIR or KEEPONE_ROOT}")}"; B="$ROOT/build"; BK="$ROOT/backup"; MAXG="${KEEPONE_MAX_GB:-14}"
FOREIGN="${KEEPONE_FOREIGN:-alt2-r}"; AGE="${KEEPONE_MIN_AGE_MIN:-120}"
MODE=check; [ "$1" = "--list" ] && MODE=list
[ "$1" = "--clean-sockets" ] && MODE=clean
[ "$1" = "--apply" ] && MODE=apply
SKEEP="${KEEPONE_SUSPEND_KEEP:-3}"
# --apply runs itself once under the run lock (lockf -t 0 as the series scripts do); lockf rc 73/75 = lock not available
if [ $MODE = apply ] && [ -z "$KEEPONE_LOCKED" ]; then
	[ -d "$B" ] || { echo "KEEP-ONE apply skipped: no $B"; exit 3; }
	KEEPONE_LOCKED=1 lockf -t 0 "$B/.darling-run.lock" sh "$0" --apply; _rc=$?
	case $_rc in 73|75) echo "KEEP-ONE apply skipped: $B/.darling-run.lock is held (a series is running)"; exit 3;; esac
	exit $_rc
fi
# stale mldr RPC sockets: /tmp/darling-mldr-<pid>-<fd>, a socket, pid dead, older than 30 min (every condition re-checked per entry)
stale_sock() { # $1 = path
	[ -S "$1" ] || return 1
	_b=${1##*/}; _x=${_b#darling-mldr-}; _pid=${_x%%-*}
	case "$_pid" in ''|*[!0-9]*) return 1;; esac
	kill -0 "$_pid" 2>/dev/null && return 1
	[ -n "$(find "$1" -mmin +30 2>/dev/null)" ]
}
if [ $MODE = clean ]; then
	rm_n=0; live=0
	for _f in /tmp/darling-mldr-[0-9]*-[0-9]*; do
		[ -S "$_f" ] || continue
		if stale_sock "$_f"; then { rm -f "$_f" 2>/dev/null || sudo -n rm -f "$_f" 2>/dev/null; } && [ ! -e "$_f" ] && rm_n=$((rm_n+1)); else live=$((live+1)); fi
	done
	echo "KEEP-ONE sockets removed=$rm_n kept_live=$live"; exit 0
fi
ns=0; for _f in /tmp/darling-mldr-[0-9]*-[0-9]*; do stale_sock "$_f" && ns=$((ns+1)); done

# dirs holding sources that are not in git (cc.sh, link.sh, *.c, *.m, *.s, headers)
KEEP_SRC="dyld-raw *-relink* lsk-* me-lsk marker-suppl suppl-stubs* supl-bind* un-bind atomic-* extras-noobjc objc-suppl wl-debug-src"
# dirs the scripts write to
KEEP_OUT="dserver real-macho crash-probe coredata-coreservices wayland-window-test wayland-backend quartzcore stub-data cores src"
# build infrastructure and per-script scratch/output dirs (recreated by their scripts)
KEEP_INFRA="CMakeFiles Developer -I i386 x86_64 x86_64-apple-darwin20 dtape cg-supl suppl wl-debug-copy wlr-protocols-gen constdict-desc got-slot-undoc msgSend-imp-undoc bundle-principal-class-test sc-stubs extras-objc-classes opendirectory-stub bootstrap-shim bootstrap-probe cfinit const-objc window-draw-probe screens objc-only tz-check *-probe-test bitmap-check bitmap-draw-check nscgcolor-check kqueue-close-probe font-metrics-probe throw-trace"
# work dirs of the current probes and of everything the gate (stage-gate.sh, probes/*.sh) rebuilds or uses
KEEP_PROBE="imageio imageio-probe suspend-rwlock-probe suspend-mutex-probe suspend-cv-probe suspend-wake-probe* window-draw* netsegv nullsrc unwind-resume v5store catx-kvc-probe eintr-prepost-probe psynch-ring-probe ptattach-probe semtw-probe port-construct-probe font-families-probe font-resolve-probe syscall482-probe throw-trace-selftest"
KEEP_DIRS="$KEEP_SRC $KEEP_OUT $KEEP_INFRA $KEEP_PROBE $KEEPONE_ALLOW"
# top-level build/ files that are never candidates: gate/probe output and the series fix20a, fix20, fix21, fix22
# (suspend-(rwlock|mutex|cv)-* pairs are limited to the newest KEEPONE_SUSPEND_KEEP per kind, see section 1b)
KEEP_FILES="darling.env window-draw* imageio-probe.log series-fix20*.out series-fix21*.out series-fix22*.out chrome-sr-fix20* chrome-sr-fix21* chrome-sr-fix22* $KEEPONE_ALLOW_FILES"
# backup/ entries that are rollback points (named so in the handoff notes) or not yet understood; never candidates
KEEP_BACKUPS="ImageIO ImageIO.pre-r5 ImageIO.real-r5.* stub-data-ImageIO.pre-r5 AppKitExtras.dylib.pre-b4-fontfamilies AppKitExtras.pre-resize darlingserver.pre-q1.* darlingserver.pre-b6.* darlingserver.pre-d2.* darlingserver.pre-d5.* darlingserver.pre-f1.* darlingserver.pre-rebuild libdtape.a.pre-q1 libdtape.a.pre-b6 libdtape.a.pre-d5 stubdata-pre-b5 darling-extras.dylib.pre-ctresolve libsystem_kernel.pre-statfs-deploy.* $KEEPONE_ALLOW_BACKUPS"

T=$(mktemp -t keepone.XXXXXX) || exit 2
trap 'rm -f "$T"' EXIT INT TERM

# ---- series, newest first ----
SER=""
if [ -d "$B" ]; then
	for f in $(cd "$B" && ls -t series-*.out *-series.out 2>/dev/null); do
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
	done
	printf '%s' "$_best"
}

# inodes open in a live process on the build filesystem
OPEN=" $(timeout 60 fstat -f "$B" 2>/dev/null | awk 'NR>1 && $6 ~ /^[0-9]+$/ {print $6}' | sort -u | tr '\n' ' ')"

# ---- 1. stale top-level logs/png/cores ----
if [ -d "$B" ]; then
	for f in $(cd "$B" && ls -p | grep -v / | grep -E '\.(log|png|core)$|^core\.|^chrome-sr-|^series-.*\.out$|-series\.out$'); do
		case "$f" in
		series-*.out) o=${f#series-}; o=${o%.out};;
		*-series.out) o=${f%-series.out};;
		chrome-sr-*) o=$(owner "$f");;
		*) o="";;
		esac
		case "$f" in suspend-*) continue;; esac	# suspend-(rwlock|mutex|cv) handled in 1b, other suspend-* (wake probe) protected
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
cb=$(awk -F'\t' '{s+=$1} END{print s+0}' "$T")
gb=$(awk -v k="$kb" 'BEGIN{printf "%.1f", k/1048576}')
v=pass; { [ "$ns" -gt 0 ] || [ "$nl" -gt 0 ] || [ "$nd" -gt 0 ] || [ "$nb" -gt 0 ] || [ $over = 1 ]; } && v=fail
echo "KEEP-ONE verdict=$v series=${KEEP1:-none},${KEEP2:-none} stale_logs=$nl experiment_dirs=$nd backup_dups=$nb stale_sockets=$ns candidate_bytes=$cb total=${gb}G max=${MAXG}G"
[ $v = pass ]
