# Probe post-wait darlingserver kill race audit

Scope: the 15 `build-freebsd` probe scripts named by the task (`syscall482-probe.sh` and the 14 with the same background-launch + grep-wait pattern). Analysis only; no probe code changed.

Source inspected: current `origin/pr-arm64` worktree scripts; line numbers below are from that tree.

## Shared launch/wait contract

`build-freebsd/probe-launch.sh` defines `probe_launch LOG WAIT PATTERN cmd...`:
- truncates `LOG` synchronously (`: > "$_pl_log"`);
- runs `cmd ... >> "$_pl_log" 2>&1 &`, records launcher PID (`_pl_pid`);
- polls every 1s: return 0 if `PATTERN` seen, return 1 if launcher process is gone/zombie without pattern, return 2 on timeout.
- It does **not** kill the launched command on pattern match/timeout, and does **not** record darlingserver PIDs.

Every audited probe does: build test binary -> capture `PRE` darlingserver PIDs -> `probe_launch ... sudo timeout -k 5 ... $BD/launch-dynamic` -> `sleep 1` -> kill every darlingserver PID not in `PRE` -> parse verdict from log.

Kill snippet (all 15, except quoting/style differences noted):

```sh
PRE=" $(pgrep -x darlingserver | tr ... ) "
...
probe_launch "$LOG" "$WAIT" "..." sudo timeout -k 5 "$WAIT" env ... "$BD/launch-dynamic"
sleep 1
for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done
```

## Per-script table

| Script | PRE capture (line, quote) | probe_launch (line) | post-wait kill -9 (line) | PRE contains | Normal run: PIDs targeted | Early break by stale log: PIDs targeted | Orphaned timeout-wrapped launch: PIDs targeted |
|---|---|---:|---:|---|---|---|---|
| `build-freebsd/bitmap-check.sh` | L22: `PRE=" $(pgrep -x darlingserver | tr '\n' ' ') "` | L24: pattern `BITMAP-PROBE DONE\|FATAL signal`, `timeout -k 5 "$WAIT"` | L27: `for p in $(pgrep -x darlingserver); do case "$PRE" in *" $p "*) ;; *) sudo kill -9 "$p" 2>/dev/null;; esac; done` | Space-delimited darlingserver PIDs present before launch | Any darlingserver not in PRE: includes this run's server(s), also any unrelated new darlingserver started after PRE | If stale old log line matched before truncation/race or probe exits early on old summary, cleanup can kill a newly started real server not in PRE; if it breaks before server starts, kills nothing and leaves launcher/timeout wrapper as-is | Launcher already gone or pattern timeout returned; kill loop still SIGKILLs every non-PRE darlingserver. If timeout wrapper/launch-dynamic left descendants not named `darlingserver`, they are not killed; if a new server appears after PRE, it is killed. |
| `build-freebsd/bitmap-draw-check.sh` | L23: same as above | L25: `BITMAP-DRAW-PROBE DONE\|FATAL signal` | L28: same kill loop | Same | Same | Same | Same |
| `build-freebsd/bootstrap-check.sh` | L17: same | L19: `BOOTCHK RESULT\|FATAL signal`, timeout `-k 5 120` (note fixed 120) | L22: same | Same | Same; may hit live run server and unrelated post-PRE servers | Same stale-log/early-break exposure | Same orphan exposure |
| `build-freebsd/cfinit-check.sh` | L32: `tr "\n" " "` | L34: `CFINIT-VERDICT\|FATAL signal` | L37: same | Same | Same | Same | Same |
| `build-freebsd/const-objc-check.sh` | L25: `tr '\n' ' '` | L27: `CONSTOBJC DONE\|FATAL signal` | L30: same | Same | Same | Same | Same |
| `build-freebsd/font-families-probe.sh` | L28: same | L30: `FAMILIES-DONE\|FATAL signal\|only defined for abstract class\|uncaught exception` | L33: same | Same | Same | Same | Same |
| `build-freebsd/font-metrics-probe.sh` | L26: same | L28: `FONTM-PROBE DONE\|FONTM-SUMMARY\|FATAL signal` | L31: same | Same | Same | Same | Same |
| `build-freebsd/font-resolve-probe.sh` | L25: same | L27: `FONTRES-DONE\|FATAL signal` | L31: same | Same | Same | Same | Same |
| `build-freebsd/kqueue-close-check.sh` | L17: same | L19: `KQCHK RESULT\|FATAL signal`, timeout `-k 5 120` | L22: same | Same | Same | Same | Same |
| `build-freebsd/nscgcolor-check.sh` | L25: same | L27: `NSCGCOLOR-PROBE DONE\|FATAL signal` | L30: same | Same | Same | Same | Same |
| `build-freebsd/port-construct-probe.sh` | L17: same | L19: `PC-SUMMARY\|FATAL signal` | L22: same | Same | Same | Same | Same |
| `build-freebsd/sc-stubs-check.sh` | L17: same | L19: `SCCHK DONE\|FATAL signal`, timeout `-k 5 120` | L22: same | Same | Same | Same | Same |
| `build-freebsd/syscall482-probe.sh` | L16: `PRE=" $(pgrep -x darlingserver | tr n  ) "` (malformed `tr n` intent; functionally different from `tr '\n' ' '`) | L18: `S482-SUMMARY\|FATAL signal` | L21: same kill loop | Same intended set, but capture is less robust/oddly quoted | Same kill-set behavior by process name | Same early-break risk; PRE formatting bug can make matching less reliable | Same orphan risk |
| `build-freebsd/throw-trace-selftest.sh` | L19: `tr "\n" " "` | L21: `SELFTEST-DONE\|FATAL signal` | L25: same | Same | Same | Same | Same |
| `build-freebsd/tz-check.sh` | L17: `tr '\n' ' '` | L19: `^TZ-VERDICT\|FATAL signal` | L22: same | Same | Same | Same | Same |

## Scenario analysis

### 1) Normal run

- `PRE` snapshots all `darlingserver` PIDs before `probe_launch`.
- `probe_launch` returns after pattern/exit/timeout; probe sleeps 1s, then kills every current `darlingserver` whose PID is not in `PRE`.
- **Kills:** this run's darlingserver process(es), plus any unrelated darlingserver started after PRE by another concurrent run/user. It does **not** target by launcher PID, PGID, cgroup, or log ownership.
- **Misses:** non-`darlingserver` descendants of the timeout-wrapped launch (e.g. stuck wrapper/helper processes not named `darlingserver`) are never targeted by this loop.

### 2) Early break by stale log

- `probe_launch` truncates the log before launch, which prevents matching old log content in normal cases. The stale-log risk remains where a probe/script exits early before this cleanup contract is effective, or where a pre-existing log content/parallel writer causes an early pattern decision; the task calls this out as a known race class.
- If the probe observes an early/stale "done/fail" condition and proceeds to cleanup before the intended server is up, the kill loop computes "all current darlingserver minus PRE". It can kill a concurrently starting real darlingserver not in PRE (including another run's), while the intended timeout-wrapped launch may still be starting or may become orphaned.
- **Kills:** any post-PRE darlingserver present at cleanup time, possibly unrelated/live.
- **Misses:** orphaned timeout wrapper/launch-dynamic process tree members not named `darlingserver`; future darlingserver started after cleanup.

### 3) Orphaned timeout-wrapped launch

- Probes launch via `sudo timeout -k 5 "$WAIT" ... launch-dynamic` under `probe_launch`; cleanup only does `sleep 1; kill -9` over `pgrep -x darlingserver` excluding PRE.
- If `timeout`/launcher exits in a way that leaves descendants, or probe exits before the wrapper finishes, there is no tracking of the wrapper PID/PGID. The cleanup does not `kill -- -PGID` and does not kill the recorded launcher.
- **Kills:** non-PRE darlingserver processes only.
- **Misses:** orphaned timeout/launch-dynamic/mldr/helpers whose process name is not `darlingserver`, and any process started after cleanup. This is the specific orphan gap named by the task.

## Minimal fix direction (not applied; stale-LOG fix on hold pending supervision word)

Record the server ownership at launch, and kill only owned processes:

1. **Capture launcher identity, not global name set:** start the probe under a dedicated process group (`setsid`/`timeout --foreground` is not enough by itself; prefer launching in a new PGID and recording it) or record `$!` from a controlled wrapper and resolve its descendant darlingserver PIDs.
2. **Kill by owned PGID/PID only:** on exit/timeout/early break, `sudo kill -TERM -$pgid`, wait briefly, then `sudo kill -KILL -$pgid` (or kill the recorded darlingserver PID after verifying command/cgroup/start time). Do not `pgrep -x darlingserver` globally.
3. **Make cleanup unconditional:** install `trap cleanup EXIT INT TERM HUP`; cleanup must run on stale-log early break, normal exit, and timeout.
4. **Stale-log guard:** keep `probe_launch`'s synchronous truncate; additionally ignore summary lines whose run token/PID does not match this launch, or write a per-run nonce into log and pattern.
5. **Do not apply now:** this is an analysis artifact for the stale-LOG race decision; code changes are intentionally deferred.

## Evidence commands

```sh
cd <repo-root>
grep -n 'PRE=\|probe_launch\|for p in $(pgrep -x darlingserver)' \
  build-freebsd/bitmap-check.sh build-freebsd/bitmap-draw-check.sh \
  build-freebsd/bootstrap-check.sh build-freebsd/cfinit-check.sh \
  build-freebsd/const-objc-check.sh build-freebsd/font-families-probe.sh \
  build-freebsd/font-metrics-probe.sh build-freebsd/font-resolve-probe.sh \
  build-freebsd/kqueue-close-check.sh build-freebsd/nscgcolor-check.sh \
  build-freebsd/port-construct-probe.sh build-freebsd/sc-stubs-check.sh \
  build-freebsd/syscall482-probe.sh build-freebsd/throw-trace-selftest.sh \
  build-freebsd/tz-check.sh
sed -n '1,120p' build-freebsd/probe-launch.sh
```
