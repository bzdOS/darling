# GUEST-STACK-ESRCH — the guest's 64 KiB stack allocation fails with an errno mldr writes itself

## Verdict (one line)

The guest dies before the window probe because the pinned `mldr` asks for a
16-page stack **with `MAP_FIXED` stripped off by its own WSL1
`MAP_FIXED_NOREPLACE` hack**: the kernel then maps the range elsewhere and
*returns success*, mldr turns that success into `errno = 3` itself, munmaps,
and prints `Failed to allocate stack of 65536 bytes: 3 (No such process)` —
the kernel never returned ESRCH, and 65536 is the pre-rlimit 16-page floor of
the same old binary.

## 1. RAW reproduction on the pinned pair

```
DARLING_SRC_DIR=... DARLING_OVERLAY=... DARLING_BUILD_DIR=<the pinned copy> \
  sh $DARLING_SRC_DIR/build-freebsd/chrome-wl-run.sh raw2 120 \
     DARLING_KEEP_LOCAL=1 DARLING_SR_RUN=1 "CHROME_ARGS=$CHROME_ARGS"
```

The pinned copy is the launcher + `dserver/` + `darling.env` that the control
logs were produced with; pointing `DARLING_BUILD_DIR` at it is what makes the
launcher exec the pinned `mldr` (`launch-dynamic` builds its own path as
`$DARLING_BUILD_DIR/dserver/mldr-real/mldr`), and it is what puts the log next
to the controls.

`/tmp/ck61-test/chrome-sr-raw2.log` (64 lines):

```
PROVENANCE git_head=16c0f18b48d2e16ce7e2a39482f8c41b568415b9 overlay_appkitextras=450ece775950b0a8dc38741b03d897b5
PROVENANCE mldr_build=bb8571303d6a4598b08a76fff1ca542d darlingserver_build=df9b743a8c3d0a9d31906b2292904f41
  26: Failed to allocate stack of 65536 bytes: 3 (No such process)
  58: Running: /tmp/ck61-test/dserver/mldr-real/mldr /tmp/darling-local-overlay/chrome-macho
  60: Failed to allocate stack of 65536 bytes: 3 (No such process)
  61: [launch] GUEST-EXIT pid=85939 status=0x100 t=0s
  63: WRAPPER_RC=1
```

Same two lines, same pair, as the controls:

```
/tmp/ck61-test/chrome-sr-ck612.log:26   Failed to allocate stack of 65536 bytes: 3 (No such process)
/tmp/ck61-test/chrome-sr-ck612.log:60   (same)
/tmp/ck61-test/chrome-sr-ts351a.log:26,60   (same)
/tmp/ck61-test/chrome-sr-ts351b.log:26,60   (same)
```

`grep -n 'Failed to allocate stack' /tmp/ck61-test/chrome-sr-*.log` prints
exactly `:26` and `:60` for every control and for `raw2`, and `raw2` has no
`BAD SEND STATUS: -61` line, which is the known-good pair's other property.

## 2. Who asks for 65536 (guest side)

The message is `src/startup/mldr/mldr.c:1179`, at the end of `setup_space()`:

```c
	if (compatible_mmap((void*)(lr->stack_top - size), size, PROT_READ | PROT_WRITE,
#ifdef DARLING_FREEBSD
	        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
#else
	        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_GROWSDOWN,
#endif
	        -1, 0) == MAP_FAILED) {
		fprintf(stderr, "Failed to allocate stack of %lu bytes: %d (%s)\n", size, errno, strerror(errno));
```

`size` comes from `rlimit(RLIMIT_STACK)` — **but not in this binary**. The
pinned `mldr` md5 `bb8571303d6a4598b08a76fff1ca542d` (530784 bytes) predates
commit `8d8f5f2bf` ("size the guest stack by the rlimit"), whose parent had:

```c
	// allocate a few pages 16 pages if it's less than the limit; otherwise, allocate the limit
	unsigned long size = PAGE_SIZE * 16;
	if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < size) {
		size = limit.rlim_cur;
	}
```

Measured in the binary, at the stack mapping (after `commpage_address()` and
`getrlimit(RLIMIT_STACK)`):

```
20767b: movq  -0x80(%rbp),%rax        # rlim_cur
20767f: cmpq  $0x10000,%rax           # compare with 65536
207685: movl  $0x10000,%ebx           # size = 64 KiB, always
20768a: cmovbq %rax,%rbx              # only a SMALLER rlimit can lower it
```

and `strings -a <pinned mldr> | grep -c 'RLIMIT_STACK is %llu'` → `0`, i.e. the
rlimit-sized version with its WARNING line is not in this binary at all. Host
limit measured for comparison: `ulimit -s` → `524288` (as freebsd and under
`sudo -n`), so an rlimit-sized stack would have been 8 MiB, not 64 KiB.

## 3. Who gives errno 3 — mldr, not the kernel

Same block, immediately after the `mmap` call (the inlined `compatible_mmap`):

```
2076a3: movl  $0x1002,%ecx            # flags = MAP_ANON|MAP_PRIVATE — no MAP_FIXED (0x10)
2076a8: movl  $0xffffffff,%r8d         # fd = -1
2076ae: xorl  %r9d,%r9d               # offset = 0
2076b1: callq mmap@plt
2076b6: cmpq  %r15,%rax               # result == requested addr?
2076b9: sete  %cl
2076bc: cmpq  $-0x1,%rax              # result == MAP_FAILED?
2076c0: sete  %dl
2076c3: orb   %cl,%dl
2076c5: jne   0x2076e4                # either one -> trust the result
2076c7: callq __error@plt
2076cc: movl  $0x3,(%rax)             # errno = 3 (ESRCH)  <- written here
2076d2: movq  %r15,%rdi
2076d5: movq  %rbx,%rsi
2076d8: callq munmap@plt              # unmap what the kernel mapped elsewhere
2076dd: movq  $-0x1,%rax              # MAP_FAILED
2076e4: cmpq  $-0x1,%rax
2076e8: je    0x207e64                # -> the "Failed to allocate stack" print
```

The `jne` at `2076c5` is the proof: the `errno = 3` store only runs when
`result != requested && result != MAP_FAILED`, i.e. **when `mmap` returned a
valid address**. This is the WSL1 `fixed_noreplace_hack` of `compatible_mmap`
as it looked before commit `0f6878684` ("fix(mldr/freebsd): skip WSL1
MAP_FIXED_NOREPLACE hack"), whose parent had:

```c
	bool fixed_noreplace_hack = false;
	if ((flags & MAP_FIXED_NOREPLACE) && !is_kernel_at_least(4, 17)) {
		flags &= ~MAP_FIXED_NOREPLACE;
		fixed_noreplace_hack = true;
	}
	void* result = mmap(addr, length, prot, flags, fd, offset);
	...
	if (fixed_noreplace_hack) {
		if (result != addr && result != (void*)MAP_FAILED) {
			errno = ESRCH;
			munmap(addr, length);
			return MAP_FAILED;
		}
	}
```

`MAP_FIXED_NOREPLACE` is `(MAP_FIXED | MAP_EXCL)` in
`src/startup/compat-includes/sys/mman.h:11`, so clearing it clears
`MAP_FIXED` — which is exactly why the flags constant that reaches `mmap` is
`0x1002` instead of `0x1012`.

Kernel side, asked the same question directly (`mmap(0x7fffffdff0000, 65536,
PROT_READ|PROT_WRITE, …, -1, 0)` — `commpage - 65536`, the address the guest
stack wants):

```
flags=MAP_PRIVATE|MAP_ANON             0x1002 want=0x7fffffdff0000 ret=0x1000     errno=0 (No error: 0)
flags=MAP_PRIVATE|MAP_ANON|MAP_FIXED   0x1012 want=0x7fffffdff0000 ret=0xffffffffffffffff errno=22 (Invalid argument)
```

With the flags mldr actually passes, the kernel answers with a **valid
address and errno 0**; with `MAP_FIXED` present it answers `EINVAL`. Neither
answer is `3` — ESRCH is not a `mmap(2)` error on this kernel, and the
disassembly above shows where the number comes from.

## 4. What HEAD already has

Commit `0f6878684` (2026-06-23, an ancestor of the current tip) put the
`#ifdef DARLING_FREEBSD` early return in `compatible_mmap`, and `8d8f5f2bf`
replaced the 16-page floor with the rlimit rule. The current build's `mldr`
(md5 `ed685cda67ac7b9451281f98e30f5e3f`) carries both: a run through it prints

```
[darling-mldr] stack region 0x7fffff600000..0x7fffffe00000 (8388608 bytes) is free (no vm entry under it at all)
```

and has no `Failed to allocate stack` line of its own (log
`$DARLING_BUILD_DIR/chrome-sr-raw1.log`). The failure the control logs and
`raw2` show is therefore a property of the **pinned** pair's `mldr`, not of
the tree at `16c0f18b4`.

## 5. Not measured, next

- The fix path is a separate dispatch: nothing here decides whether the pin
  should be refreshed, or how the run should be pointed at a current `mldr`.
- Why `MAP_FIXED` on exactly `0x7fffffdff0000` answers `EINVAL` in a plain
  host process while neighbouring addresses succeed was not chased; it does
  not enter the guest's path, which never gets that far with `MAP_FIXED`.
- Not measured outside this host, and one raw run was spent on the pin.
