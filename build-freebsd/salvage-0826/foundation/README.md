# foundation — recovered commits

Patches: 0001-NSData-don-t-reference-NSURLConnection-NSURLRequest-.patch,
         0002-NSUserDefaults-actually-persist-setPersistentDomain-.patch

- Base: a8b5ab193a107712d0806b6f95bfa763be68e1f8 (upstream:
  darlinghq/darling-foundation, "Add Info.plist file (#31)")
- Recovered commits: 85ed2f7e8a4e7032f68c42fe094c6f6728a30dd7 and
  89905cf9dcedb1ece42b40badc6dd95d7a44fd0c
- Special case — the original base 59fc8c984bce5662ec39e0c3a3cb7dfce2927eec
  is not present in upstream (fetching that object is refused); it was a
  local commit. Recovery therefore stands on the most recent upstream
  commit that all recorded edits apply to cleanly (a8b5ab193; all five
  edits replayed without adjustment) and recreates the lost base's
  probable content — the NSData.m and NSPlatform_posix.m changes — as the
  first commit.
- The first commit's message is reconstructed: it uses the NSData commit
  message found in the session log, plus a paragraph describing the
  NSPlatform_posix.m changes, because the lost base's own commit message
  is unavailable. The second commit (NSUserDefaults.m) carries the verbatim
  message from commits.md.
- Edits applied: 5/5.
- Provenance: session log edit records (edits.json) and commit-message
  transcript (commits.md) kept outside this repository.

## Build result (build-freebsd/build-foundation-only.sh)

Built successfully. 231 object files compiled with zero errors (only
version warnings from the staged system libraries at link time); the
result is a Mach-O 64-bit x86_64 dynamically linked dylib, installed into
the overlay at System/Library/Frameworks/Foundation.framework/Versions/C/
Foundation. The previous overlay copy of Foundation was preserved before
the overwrite.
