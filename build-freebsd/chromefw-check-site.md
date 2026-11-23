# Chrome Framework CHECK site static analysis (r24)

Scope: static only, no VM/runtime changes. Inputs: `chrome-sr-r24.log` and local Chrome for Testing binaries (Framework + main executable). Commands below use placeholders:
- `$FW` = Chrome Framework binary
- `$CHROME` = Chrome main executable binary
- `$LOG` = `chrome-sr-r24.log`

## 1) Primary SIGTRAP site (first delivery)

Log evidence:
- `$LOG:25075` `[darling-mldr] SIGTRAP-AUDIT-DELIVER: native SIGTRAP -> guest handler=0x354ddc39bbe0 (linux_signo=5) rip=0x354ddf18d378 si_code=1 si_addr=0x354ddf18d378`
- `$LOG:2326` `[dyld-trace] *** add-image #45  mh=0x354ddd353000 slide=0x354ddd353000  total_images=134`
- `$LOG:2371` `[dyld-trace]   [44] mh=0x354ddd353000 Google Chrome for Testing Framework <-NEW`

Address mapping:
- Framework slide = `0x354ddd353000`, vmaddr base = `0x0` (Mach-O `__TEXT` vmaddr 0), so unslid/file offset = `rip - slide = 0x1e3a378`.

### nm neighborhood

Command:
```sh
llvm-nm --defined-only -n "$FW" | head
python3 -c "..." # bisect around 0x1e3a378 in nm -an output
```

Output:
```text
0000000000002840 T _ChromeAppModeStart_v8
0000000000002a00 T _ChromeWebAppShortcutCopierMain
0000000000003fe0 T _ChromeMain

nearest before 0x1e3a378: 0x3fe0 T _ChromeMain
nearest after  0x1e3a378: (none; only 3 defined symbols in nm)
```

### Disassembly around unslid 0x1e3a378

Command:
```sh
llvm-objdump -d --start-address=0x1e3a300 --stop-address=0x1e3a400 "$FW"
```

Output (excerpt):
```text
0000000000003fe0 <_ChromeMain>:
 ...
 1e3a341: 48 89 c3                     	movq	%rax, %rbx
 1e3a344: eb 13                        	jmp	0x1e3a359 <_ChromeMain+0x1e36379>
 1e3a346: 48 89 c3                     	movq	%rax, %rbx
 1e3a349: f0                           	lock
 1e3a34a: 41 ff 4d 00                  	decl	(%r13)
 1e3a34e: 75 09                        	jne	0x1e3a359 <_ChromeMain+0x1e36379>
 1e3a350: 49 8b 06                     	movq	(%r14), %rax
 1e3a353: 4c 89 f7                     	movq	%r14, %rdi
 1e3a356: ff 50 08                     	callq	*0x8(%rax)
 1e3a359: 48 8d 7d d0                  	leaq	-0x30(%rbp), %rdi
 1e3a35d: e8 ee 21 a0 ff               	callq	0x183c550 <_ChromeMain+0x1838570>
 1e3a362: 48 89 df                     	movq	%rbx, %rdi
 1e3a365: e8 6e 4c d8 0b               	callq	0xdbbefd8 <dyld_stub_binder+0xdbbefd8>
 1e3a36a: 4d 8b 76 08                  	movq	0x8(%r14), %r14
 1e3a36e: e9 5f fc ff ff               	jmp	0x1e39fd2 <_ChromeMain+0x1e35ff2>
 1e3a373: 0f 0b                        	ud2
 1e3a375: eb cf                        	jmp	0x1e3a346 <_ChromeMain+0x1e36366>
 1e3a377: cc                           	int3
 1e3a378: 0f 0b                        	ud2
 1e3a37a: cc                           	int3
 1e3a37b: 0f 0b                        	ud2
 1e3a37d: eb c2                        	jmp	0x1e3a341 <_ChromeMain+0x1e36361>
 1e3a37f: cc                           	int3
 1e3a380: 0f 0b                        	ud2
```

Wider control flow (same objdump session):
```text
 1e3a202: 3d ff ff ff 7f               	cmpl	$0x7fffffff, %eax       ## imm = 0x7FFFFFFF
 1e3a207: 0f 84 6d 01 00 00            	je	0x1e3a37a <_ChromeMain+0x1e3639a>
 ...
 1e3a2c8: 83 e8 01                     	subl	$0x1, %eax
 1e3a2cb: 41 89 07                     	movl	%eax, (%r15)
 1e3a2ce: 0f 82 f2 00 00 00            	jb	0x1e3a3c6 <_ChromeMain+0x1e363e6>
 1e3a2d4: 74 48                        	je	0x1e3a31e <_ChromeMain+0x1e3633e>
 ...
 1e3a3c6: cc                           	int3
 1e3a3c7: 0f 0b                        	ud2
 1e3a3c9: cc                           	int3
 1e3a3ca: 0f 0b                        	ud2
```

### Verdict (1,2)

- **CHECK/trap site identified by address/class, not by source string:** first delivery rip `0x354ddf18d378` maps to Framework unslid `0x1e3a378` (`ChromeMain+0x1e36398` in objdump labels), inside a `cc/ud2` trap island. Nearby logic is atomic refcount increment/decrement guards (`lock xaddl` / `decl` with overflow/underflow branches to trap islands at `0x1e3a373..0x1e3a380` and `0x1e3a3c6..0x1e3a3ca`).
- **No CHECK message string found at the trap site:** disassembly contains no RIP-relative `lea` to a string near `0x1e3a378`; `strings -t x` has no readable message at/near that offset. This is consistent with immediate crash/ud2 trap, not a formatted CHECK message.
- **Function name:** nearest nm symbol is `_ChromeMain` (`0x3fe0 T`). The binary exposes only 3 defined symbols; no local symbol names are available around `0x1e3a378`, so a more specific function name cannot be proven from nm alone.

## 2) rt_sigaction(SIGTRAP, SIG_DFL) source site (0x226605)

Log evidence:
- `$LOG:93`  `rt_sigaction(5) from 0x226605 <unknown> old=0x0 new=0x82854b940`
- `$LOG:182` `rt_sigaction(5) from 0x226605 <unknown> old=0x82854b940 new=0x354ddc39bbe0`
- `$LOG:25095` `rt_sigaction(5) from 0x226605 <unknown> old=0x354ddc39bbe0 new=0x0` (SIG_DFL before fatal delivery)

Address mapping attempts:
- Not in any dyld-trace image range (scan of `mh=` entries in `$LOG`).
- Not inside any `patch_linux_raw_syscalls` mapped executable range.
- Chrome main executable `__TEXT` is `vmaddr=0x100000000 size=0x4000` (file offsets 0..0x4000); `0x226605` is outside it.
- Framework `__TEXT` vmaddr is `0x0`, so raw `0x226605` is a valid file offset candidate.

### Disassembly by raw file offset

Command:
```sh
llvm-objdump -d --start-address=0x226580 --stop-address=0x226640 "$FW"
llvm-objdump -d --start-address=0x226580 --stop-address=0x226640 "$CHROME"
```

Framework output (excerpt):
```text
  226580: 0f 85 b2 07 00 00            	jne	0x226d38 <_ChromeMain+0x222d58>
  226586: 41 f6 c1 01                  	testb	$0x1, %r9b
  ...
  226604: 4c 8b bd 68 ff ff ff         	movq	-0x98(%rbp), %r15
  22660b: 4a 8d 04 fd 00 00 00 00      	leaq	(,%r15,8), %rax
```

Chrome main executable output: empty disassembly in that range.

Verdict: `0x226605` is **not confidently attributable** to a named module/function. It is outside dyld-trace image ranges; as a raw Framework file offset it lands in `ChromeMain` text, but no symbolized function name or SIGTRAP-handler-related string/call can be proven. `rt_sigaction(5,...,new=0)` at `$LOG:25095` is observed, but the caller remains `<unknown>`.

## 3) Second delivery chain frame #0 (0x225bd1)

Log evidence:
- `$LOG:25096` fatal SIGTRAP delivery: `rip=0x22dbec si_code=65537 si_addr=0x0`
- `$LOG:25097-25101` chain: `#0 ret=0x225bd1`, `#1 0x82354d45a`, `#2 0x82354ca5b`, `#3 0x820ed82d3`
- `$LOG:25102` `GUEST-EXIT pid=11888 status=0x5 t=24s`

Address mapping attempts:
- `rip=0x22dbec`: not in any dyld-trace image range. Chrome-macho `__TEXT` is 0x4000 bytes at vmaddr 0x100000000; unslid 0x10022dbec is outside text. Framework file-offset symbolization gives `ChromeMain` at 0x22dbec (`llvm-symbolizer --obj "$FW" 0x22dbec` => `ChromeMain`, `??:0:0`) but slid would be `0x354ddd37ebec`, which does not equal raw rip; therefore **not reliable**.
- `ret=0x225bd1`: also outside dyld-trace image ranges; raw Framework offset disassembles in `ChromeMain` text.

Command/output (raw Framework offset probe):
```sh
llvm-objdump -d --start-address=0x225b80 --stop-address=0x225c20 "$FW"
...
  225bce: 49 c1 e9 03                  	shrq	$0x3, %r9
  225bd2: 45 39 c8                     	cmpl	%r9d, %r8d
  225bd5: 4c 8b 7d c0                  	movq	-0x40(%rbp), %r15
  225bd9: 48 8b 5d b8                  	movq	-0x48(%rbp), %rbx
  225bdd: 0f 85 c5 e3 ff ff            	jne	0x223fa8 <_ChromeMain+0x21ffc8>
```

Verdict: second-delivery `rip` and chain frame #0 are **not confidently symbolizable** to module+function. Raw offsets fall in Framework `ChromeMain` byte range, but they do not map to any dyld-trace image range and cannot be validated as slid addresses.

## 4) Hypothesis: related to parent/rendezvous after getppid fix?

**No evidence from static site text.** The identified first-delivery trap island at `0x1e3a378` is an atomic refcount overflow/underflow ud2 cluster inside `_ChromeMain` (no readable CHECK string, no parent/process/rendezvous string nearby in disassembly). The observed sequence is: first SIGTRAP delivered (`si_code=1`, Framework+0x1e3a378), then `rt_sigaction(5, SIG_DFL)` from unknown `0x226605`, then fatal SIGTRAP (`si_code=65537`, rip `0x22dbec`) leading to `GUEST-EXIT status=0x5`. This proves a trap/SIGTRAP handling collapse, but the static text at 0x1e3a378 does not show parent/rendezvous/getppid logic. Connection to rendezvous/getppid fix remains **unproven** by this static analysis.
