# chrome-workload: restore the Chrome workload, then the control

## Workload restored (step 0–1)

Step 0 found no surviving Chrome artifact (no `chrome-cft`, `chrome-mac-x64`
or `.app` under the temp/tree/storage roots). Step 1 downloaded the public
artifact:

```
https://storage.googleapis.com/chrome-for-testing-public/154.0.8029.0/mac-x64/chrome-mac-x64.zip
sha256 (local) e4e8b07232b9f86998e20b789258c00500b988591ea7ab7b3c70a890b15cc11d
size 201299597 bytes; unzip -t: No errors detected
```

The per-version CfT JSON (`…/chrome-for-testing/154.0.8029.0.json`) carries
the download URL but **no `sha256` field** (and `<url>.sha256` is a
`NoSuchKey`), so the checksum was verified by `unzip -t` instead. Extracted
to `/tmp/chrome-cft/chrome-mac-x64/Google Chrome for Testing.app` (matches
`launch-chrome.sh`'s default); the framework is `154.0.8029.0`, 267 MB.
Staging is done by the harness itself.

## Control — not reproduced

`sh build-freebsd/launch-chrome.sh` with the restored app on the **live**
overlay runs, and the guest dyld-trace loads **41 images** — then fails at

```
dlopen //../Frameworks/Google Chrome for Testing Framework.framework/Versions/154.0.8029.0/Google Chrome for Testing Framework:
dlopen(…, 261): image not found.
```

There is **no `FATAL signal 11`** and no `notifyBatchPartial` — the base
crash (0x10005e6d4) is not reproduced. The staged framework binary is
present (267 MB) with its `Versions/Current` symlinks, so this is a path
resolution failure in the guest, not a missing artifact.

Note: the live dyld is the June build (`b8df2a76`, restored last turn),
which runs `hello-dynamic-macho` cleanly; the prior doc's 0x10005e6d4
crash was measured on the thin pre-salvage image `ad4850c1`
(`usr/lib/dyld.pre-salvage-20260926`). The base crash is tied to that
image, not to the current live dyld.

## Verdict (one line)

Workload restored from `storage.googleapis.com` (154.0.8029.0, sha256
`e4e8b072…`, `unzip -t` OK); control **not reproduced** — the live June
dyld loads 41 images then fails at the Chrome framework `dlopen` (image
not found) with no FATAL, so the Extras bisection is not started.
