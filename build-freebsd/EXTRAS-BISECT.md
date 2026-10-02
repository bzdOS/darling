# extras-bisect: control not reproduced (no Chrome app; live dyld does not crash)

Wall #6, route "bisect the Extras stubs on the live dyld".

## Step 0 — resolve 0x10005e6d4

The live `usr/lib/dyld` is a **stripped** Mach-O (0 symbols). At
0x10005e6d4 (offset 0x5e6d4 from `__TEXT` 0x100000000) the disassembly
lands in a large inlined function: stack-relative buffers, `leaq` into
rsi/rdi, `movl $0x200, %edx`, calls to small helpers (a 512-byte
copy-like path) — not a distinguishable Logging-printf. So step 0 does
**not** close the route by fact.

## Control — not reproduced

- **No Chrome app.** `launch-chrome.sh`'s `CHROME_APP` default path does not exist; no
  Chrome `.app` is present under the temp, tree or storage roots (only
  the framework already staged in the overlay). So the chrome probe cannot be run.
- **The live dyld does not crash.** `hello-dynamic-macho` (the prior
  doc's second control) on the live overlay exits **rc=0** — no `FATAL
  signal`. The base crash (0x10005e6d4) is not reproduced.

## Incident (reported)

The id-53 gate copied the new dyld into an overlay copy made with
`cp -al`; the destination was a **hard link** to the live
`usr/lib/dyld`, and the `cp` wrote the rebuilt image through it — the
live overlay's dyld became the dedup-rebuilt one (md5 `d1e45ba9…`),
which crashes the guest at 0x1000fce90. This turn it was restored to the
pre-gate state (md5 `b8df2a76…`, the June build, from
`usr/lib/dyld.June-backup`), the hard-linked copy was removed, and the
live dyld now runs `hello-dynamic-macho` rc=0.

For reference the overlay holds: `dyld.June-backup` / `dyld.bak`
(`b8df2a76`, fat June build), `dyld.pre-salvage-20260926` (`ad4850c1`,
the thin image the prior doc's 0x10005e6d4 crash was measured on), and
`build/dyld-raw/dyld-orig-June-build-backup` (`b8df2a76`).

## Verdict (one line)

**Control not reproduced** — the Chrome app is absent and the restored
live dyld runs the probe cleanly (rc=0), so the base crash is not
available; per the order ("без контроля дальше не идти") the Extras
bisection is **not started**. step-0: 0x10005e6d4 is a stripped, inlined,
512-byte copy-like path, not a Logging-printf.
