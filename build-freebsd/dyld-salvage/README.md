# dyld September changes, carried as full files

`src/external/dyld` is a **submodule** in this repository: the superproject
holds a gitlink, never the files. Work done inside that submodule's working
tree therefore cannot be committed here —

    fatal: Pathspec 'src/external/dyld/src/dyld2.cpp' is in submodule 'src/external/dyld'

— and the usual escape (commit in the submodule, bump the gitlink) was not
available: the submodule's object store did not contain the pinned base
commit, so no diff against it could be produced offline, and no patch could be
generated from it.

So the modified files are carried here verbatim, under the same relative paths
they have inside the submodule.

## What is in here

| File | What it is |
|---|---|
| `src/external/dyld/src/dyld2.cpp` | full file, with the diagnostic/logging patch used by the FreeBSD raw-clang dyld runs |
| `src/external/dyld/src/dyldFreeBSDRebase.c` | new file: manual classic-rebase applier (ld64.lld emits an empty chained-starts payload, so dyld's own chained path silently does nothing) |
| `src/external/dyld/src/dyldInitialization.cpp` | full file, calling the rebase above from the raw-clang build path |
| `src/external/dyld/darling/src/sandbox-dummy.c` | new file: no-op `sigexc_setup()` / `sandbox_check()` so the raw-clang dyld links without libsystem_platform's exception-port setup |
| `src/external/dyld/CMakeLists.txt` | the `system_loader` link line with `-Wl,-fixup_chains` replaced by `-Wl,-no_fixup_chains`, so the image gets classic rebase and bind opcodes the way the raw build did — see `build-freebsd/dyld-rebuild.md` |

They are whole files, not hunks: with the base commit unavailable the exact
change could not be isolated, so treat them as "copy over the submodule
worktree" material.

## How to put them back

    git submodule update --init src/external/dyld
    ( cd build-freebsd/dyld-salvage && \
      find . -type f \( -name '*.[ch]' -o -name '*.cpp' -o -name 'CMakeLists.txt' \) \
      -exec cp --parents {} /tmp/dyld-salvage-stage/ \; )
    ( cd /tmp/dyld-salvage-stage && tar cf - . ) | ( cd src/external/dyld && tar xf - )

Then commit inside the submodule, push that branch, and bump the gitlink here.
That last step is an infrastructure decision (a new public repository and
copies to seed it), not something to do from a build host.

## Note on strings

Host names, ssh key names and internal share paths that appeared in these
files have already been replaced with neutral text. A scan of this directory
for them comes back empty.
