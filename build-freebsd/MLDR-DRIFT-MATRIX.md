# MLDR-DRIFT-MATRIX — which build input makes the rebuilt mldr report -61

## Verdict (one line)

**None of the four inputs**: patch-state, build cache, flags/strip and toolchain
all come out the same on both sides, and `-61` survives every one of them — the
input that separates the runs is the **vintage of the `mldr` source in the pin**,
which predates both stack-fix commits, not anything about how the binary is built.

## 1. The matrix

One harness, one build-dir copy, one `mldr` variant per run, same trunk
(`git_head` of every run below) and the **same `darlingserver` in every row**, so
only `mldr` moves between rows.

| input | variant | md5 | size | `-61` count | `checkin` lines | log |
|---|---|---|---|---|---|---|
| (reference) known-good pin | `v2pin` | `bb8571303d6a4598b08a76fff1ca542d` | 530784 | **0** | 0 | `$DARLING_BUILD_DIR/chrome-sr-b3v2pin.log` |
| trunk as-is | `v0` | `ed685cda67ac7b9451281f98e30f5e3f` | 841680 | 1 | 2 | `$DARLING_BUILD_DIR/chrome-sr-b3v0.log` |
| (c) debug+symtab stripped | `v1` | `43ff2cff972525325bcfb44eb9e1814a` | 196824 | 2 | 4 | `$DARLING_BUILD_DIR/chrome-sr-b3v1.log` |
| (b) clean rebuild | `v2` | `fcf15f19bf2520f75b439bdf6baac7bb` | 841680 | 2 | 4 | `$DARLING_BUILD_DIR/chrome-sr-b3v2.log` |

Matrix file: `$DARLING_BUILD_DIR/MATRIX.log` (5 lines),
sha256 `2d7d5ead8d85c744558acf447a741532272985c0a261587615a7015df74bfe0d`.

PROVENANCE of every run (identical `git_head`, each row naming its own binary):

```
v0     PROVENANCE git_head=b5807696b21e22dd365b9ccfab9764d65430df52
v0     PROVENANCE mldr_build=ed685cda67ac7b9451281f98e30f5e3f darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
v1     PROVENANCE git_head=b5807696b21e22dd365b9ccfab9764d65430df52
v1     PROVENANCE mldr_build=43ff2cff972525325bcfb44eb9e1814a darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
v2     PROVENANCE git_head=b5807696b21e22dd365b9ccfab9764d65430df52
v2     PROVENANCE mldr_build=fcf15f19bf2520f75b439bdf6baac7bb darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
v2pin  PROVENANCE git_head=b5807696b21e22dd365b9ccfab9764d65430df52
v2pin  PROVENANCE mldr_build=bb8571303d6a4598b08a76fff1ca542d darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
```

`grep -c -- '-61' <log>` → `v0 1`, `v1 2`, `v2 2`, `v2pin 0`: the empty `-61`
grep belongs to the pin row alone.

Run recipe (same for every row, only the binary in the build-dir copy changes):

```
sh $DARLING_SRC_DIR/build-freebsd/chrome-wl-run.sh b3<variant> 120 \
   DARLING_KEEP_LOCAL=1 DARLING_SR_RUN=1 "CHROME_ARGS=$CHROME_ARGS"
```

## 2. Input (a) patch-state — no variance

```
sh build-freebsd/apply-patches.sh  ->  APPLY_RC=0
  APPLIED=0  ALREADY=144  FAIL=0  REPLACED=0
```

All seven root patches of `build-freebsd/patches/series` were already in the tree
before the rebuild: six answer `ALREADY` under `git apply --reverse --check`, and
`mldr-sig65-unblock-count.patch` reports `NOT-APPLIED` by that check only because
its content is folded into the source — `grep -c 'sigaddset.*65'` in
`src/startup/mldr/freebsd_syscall_trap.c` → `1`, `grep -c 'mldr_sig65_tramp_count'`
in `sigaction_translate.c` → `2`, which is exactly the branch
`apply-patches.sh` treats as `ALREADY`. Nothing was applied, replaced or reordered,
and the rebuild that ran on this state still produced `-61` (row `v2`).

## 3. Input (b) build cache — no variance, and the cache is not in the path

`build-freebsd/build-mldr-only.sh` builds in a scratch directory it deletes first
(`rm -rf "${MLDR_TMP_BUILD}"` where `MLDR_TMP_BUILD="/tmp/mldr-build-$$"`), so
every run of it is a clean build by construction; it completed with `BUILD_RC=0`
and installed `fcf15f19bf2520f75b439bdf6baac7bb`.

Two builds of the same source differ in md5 and agree in size (841680 both), and
both report `-61` — the build is not the variable.

The stale-looking objects in `$DARLING_BUILD_DIR/dserver/mldr-build-temp/`
(`mldr.c.o` 11-24 00:45 against `mldr.c` 11-24 09:34) are not in any build path:
`grep -rn 'mldr-build-temp' build-freebsd/*.sh` → empty.

## 4. Input (c) flags / strip — no variance in behaviour, and the size drift is not "just debug"

Stripping debug and symtab (841680 → 196824) changes nothing about `-61`
(row `v1`). The 518k → 822k drift splits into two parts, measured by section:

```
                 pin            trunk build
.text             89840          146848      (+57008 real code)
.rodata            5216           22160      (+16944)
.debug_*         399618          621256      (+221638)
.symtab            7416           11544
```

so roughly half the byte drift is debug sections and half is code and rodata
that genuinely differs between the two binaries.

## 5. Input (d) toolchain — no variance

```
pin:   FreeBSD clang version 19.1.7 (... llvmorg-19.1.7-0-gcd708029e0b2)
build: FreeBSD clang version 19.1.7 (... llvmorg-19.1.7-0-gcd708029e0b2)
cc -v (this host): FreeBSD clang version 19.1.7 ... Target: x86_64-unknown-freebsd15.1
```

Identical compiler identity string in both binaries (5 occurrences each), and
neither binary embeds an `-O*` flag string, so the binaries do not record a flag
difference to compare. No second toolchain is present to build with.

## 6. The discriminating input: source vintage, not the build

The pin carries source older than both stack fixes (measured in
`GUEST-STACK-ESRCH.md` §2/§4): the WSL1 `errno = 3` hack is still in its
disassembly (commit `0f6878684`, 2026-06-23), and the rlimit sizing with its
`RLIMIT_STACK is %llu` string is absent (commit `8d8f5f2bf`, 2026-09-29), while
both commits are ancestors of the trunk the matrix ran on
(`git merge-base --is-ancestor 8d8f5f2bf 900b246c6` and
`... 16c0f18b4` → both true, `git diff --stat 900b246c6 16c0f18b4 --
src/startup/mldr` → empty).

So between the pin and every rebuilt row the only thing that changes is *which
source was compiled*; inputs (a)–(d) were each held equal and `-61` did not move.

## 7. Read the reference row carefully

`v2pin` has `checkin` lines `0`, not a successful checkin: the pin's guest dies
before reaching one (it fails the 64 KiB stack mapping and exits — see
`GUEST-STACK-ESRCH.md` §1, `chrome-sr-raw2.log` lines 26 and 60). Its `-61`
count of 0 is the absence of the event, so "known-good checkin" in this matrix
means *never reached checkin*, and `-61` is only observable in rows that get far
enough to attempt it.

## 8. Not measured

- Which source change introduced `-61`: no source was varied here, and building
  an older `mldr` source needs a second tree, which the build rules forbid.
- Whether `-61` also appears on a host-side `dserver` other than
  `df9b743a8c3d0a9d31906b2292904f41` — held fixed in every row instead.
