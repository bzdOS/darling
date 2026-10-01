# darlingserver — recovered commit

Patch: 0001-Get-the-parent-pid-from-sysctl-on-FreeBSD-return-Lin.patch

- Base: 43fd2feb615aaee1c3b9f89353dc3639941f3108 (upstream: darlinghq/darlingserver)
- Recovered commit: a7c1f3377bcd915f92af6dcaefdfb3602e11b87d
- Provenance: the commit was lost in a repository transfer. It was rebuilt
  by replaying the recorded file edits (edits.json) and using the verbatim
  commit message (commits.md), both from a preserved session log kept
  outside this repository.
- Base chain note: earlier session edits (duct-tape declarations, vchroot
  configurability, the Process::id credentials fix) are already contained
  in the base commit chain (c31abdb -> b0e7445 -> 43fd2fe); only the six
  post-base edits were replayed. A temporary diagnostic added to
  duct-tape/src/task.c during the session was reverted in the same session
  and leaves no diff.
- Composition verified against the original `git add`: src/process.cpp and
  src/call.cpp only.
- Edits applied: 6/6 post-base; 25 pre-base edits not replayed (already in
  the base by construction).

## Build result (build-freebsd/build-darlingserver.sh)

Built successfully. All sources, including the recovered src/process.cpp
and src/call.cpp changes, compiled and linked; the binary is an ELF 64-bit
x86-64 FreeBSD executable (~17 MB, with debug info). One cosmetic note:
the script's final install step reports "are the same file" because the
canonical publish path is a pre-existing symlink into the build scratch
directory — the binary is reachable through that symlink either way and
the build itself is unaffected.
