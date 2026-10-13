# Synthesis — the empty-forwarding chain, steps 33→42: what is proven, by what, and what follows

**Date:** 2026-10-13
**Branch:** task/chain-synthesis (base: pr-arm64 = 1fdf5934d)
**Scope:** the death chrome-macho dies on in `diag/chrome-fw-run.log` (Control #33),
closed by Controls #156–#164. One page: proven claims with the command that
proves each, then the consequences for the track goal.

## 1. Proven, with the command for each

**P1. The crash.** `[arg1 description]` returns a value; the next send
`[<that value> copy]` dies at `rip=libobjc+0x480DD` (`_objc_msgSend+0x1d`) with
the receiver = `Foundation+0x790B0` = `&__NSCFCopyDescription2` (a code pointer)
and `rax` a garbage class. Proved by:
`grep -n 'FATAL signal' $DARLING_SRC_DIR/../diag/chrome-fw-run.log` (block at
line 1336848) + `llvm-nm $DARLING_OVERLAY/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation | grep 790b0`.
Artifacts: `docs/OBJC-ISA-CALLER.md` (#156), `docs/OBJC-COPYDESC-PATH.md` (#157).

**P2. The class is an empty root stub.** `NSConstantDictionary` in
`/usr/lib/CoreFoundationExtras.dylib`: flags 0x2, super 0, baseMethods 0,
`method-list-count=0`. Proved by the class dump in
`docs/OBJC-DESC-IMP-BIND.md` (#158) and the runtime probe's
`SENDUNDOC stub method-list-count=0`.

**P3. The guest's forward chain is stock until the last two links.** Proved by
runtime reads in `docs/OBJC-FWD-CHAIN-CF.md` (#163):
fast path misses an empty bucket → `__objc_msgSend_uncached` (stock wrapper,
disasm 0x48c10) → `_lookUpImpOrForward(obj, sel, class, 3)` returns the STOCK
miss sentinel `_objc_msgForward_impcache` — the libobjc `__got` slot at vmaddr
0x59070 holds exactly that gadget (runtime dladdr) → `__objc_msgForward`
jumps to `*__objc_forward_handler`.

**P4. The handler is CoreFoundation's `_CF_forwarding_prep_0`, a 16-byte empty
function** (`pushq %rbp; movq %rsp,%rbp; popq %rbp; retq`, no argument touched,
no return set), installed by a CF `__mod_init_func` initializer. Proved by the
runtime read (P3) + `llvm-objdump -d --disassemble-symbols=__CF_forwarding_prep_0` +
`llvm-nm … | grep _objc_setForwardHandler` (`U`). Artifacts:
`docs/OBJC-FWD-HANDLER-ABSENT.md` (#161), `docs/OBJC-FWD-PREP-EMPTY.md` (#164).

**P5. Therefore the send's return is RAX residue, never a computed value.**
Every value any run measured is explained by P4: 0x32, 0x20, 0x1f, 0x21, 0x23,
0x26 (per selector), 0xa (next run), step 38's classname-string pointer — and
chrome's `Foundation+0x790B0`. No method ever resolved for
`[arg1 description]`; the guest never raises "unrecognized selector" for a
stub-root-class send, because CF's no-op silently replaced libobjc's fatal
default. Artifacts: `docs/OBJC-MSGSEND-MISS-PATH.md` (#162),
`docs/OBJC-FWD-PREP-EMPTY.md` (#164).

**Corollary (C1, by P1+P5): the crash is a USE of garbage, not a wrong
resolution.** Chrome's next send treated `[arg1 description]`'s return as an
object. On real macOS that return is a real string; in this guest it is
register residue. The forward path is not where the fix belongs by itself —
every link of it except the last two is stock, and the last two are one empty
function.

## 2. What follows for the track goal

The track question was "why does chrome die here"; it is answered (P1–P5). For
"chrome-macho reaches a window", the consequence is a choice among three fixes,
all downstream of C1:

1. **The class should really have the method.** `NSConstantDictionary` in the
   guest is my generated empty stub (P2). Making it carry a real
   `description` (or a proper superclass chain, so `NSObject`'s resolves) makes
   the send return an object instead of residue. This is a stub-generation fix.
2. **The guest must never silently return garbage.** `_CF_forwarding_prep_0`
   returning untouched RAX (P4) makes every unresolvable send a landmine for the
   *next* send. Restoring a raising handler (libobjc's own fatal default is the
   reference) converts silent garbage into a loud, diagnosable stop — the right
   primitive for the whole track, not just this send.
3. **Chrome's receiver should be the real class, not the stub.** If chrome's
   isa (chained-fixup ordinal 0x33d, #156) is meant to bind CoreFoundation's own
   `NSConstantDictionary` (a toll-free-bridged class with real methods), then my
   `CoreFoundationExtras` stub is shadowing it — a link-order/stub-policy fix,
   not a content fix.

**The one measurement that separates 3 from 1/2** (and it is a single offline
run, no guest): resolve chrome framework's chained-fixup import ordinal 0x33d
to the symbol AND its library ordinal — which of CoreFoundation.framework /
CoreFoundationExtras.dylib provides `_OBJC_CLASS_$_NSConstantDictionary` in
chrome's link closure. Real CF in the closure → fix 3 (shadowing). Only the
stub → fixes 1 and 2 on the stub path.

## 3. What is explicitly NOT proven here

- Which image chrome's 0x33d ordinal binds to (§2's measurement, not yet run).
- Whether either fix unblocks chrome's run past this send: each fix must be
  re-measured by the same recipe (run chrome-macho, new log, compare the death
  or the absence of it).
- Anything about later blockers after this send — the run has not gotten past
  it, so none is observed.

## 4. Repro map (where each command lives)

Every claim above has its command in the artifact doc it cites:
#156 `docs/OBJC-ISA-CALLER.md` · #157 `docs/OBJC-COPYDESC-PATH.md` · #158
`docs/OBJC-DESC-IMP-BIND.md` · #160 `docs/OBJC-DESC-IMP-PROBE.md` · #161
`docs/OBJC-FWD-HANDLER-ABSENT.md` · #162 `docs/OBJC-MSGSEND-MISS-PATH.md` · #163
`docs/OBJC-FWD-CHAIN-CF.md` · #164 `docs/OBJC-FWD-PREP-EMPTY.md`.

---

## 5. Addendum 2026-10-13 (Control #167): the exe/dylib bind asymmetry, a track finding of its own

Measured live while fixing the chain (F1–F3 of `docs/OBJC-STUB-METHODS-FIX.md`),
and worth its own paragraph because it constrains every future fix that leans
on another image's symbol:

- **F1.** A guest EXECUTABLE linked against libobjc binds both
  `_OBJC_CLASS_$_NSObject` (→ libobjc+0x5c050) and `_OBJC_METACLASS_$_NSObject`
  (→ libobjc+0x5c000) at load with no error.
- **F2.** A guest DYLIB holding the same two references (the wrapper's own
  link line: one class `: NSObject` + `-reexport_library` CoreFoundation +
  libobjc by full path) keeps them as unbound imports (`super=0` in the file
  dump) and the guest refuses to load it: `dyld: Symbol not found:
  _OBJC_METACLASS_$_NSObject` → `abort_with_payload` at image load. So the
  wrapper link is NOT anomalous; the asymmetry between the executable path
  and the dylib path in the guest's two-level bind is itself the fact.
- **F3.** The failing name was the metaclass's (the class symbol was never
  reported missing).

Until this is explained, any fix whose success depends on a dylib binding a
class symbol from another image is not a fix — which is exactly why the
chosen fix (the stub's own methods, Control #167) carries zero load-time
binds. Status: measured and unexplained, owned by this track.
