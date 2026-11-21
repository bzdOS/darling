#!/bin/sh
# gen-from-schema-test.sh - a single quote in a series chrome_extra_args (run-*.env values are single-quoted) must be
# rejected by the schema: gen-from-schema.py --check prints "SCHEMA-DRIFT FAIL schema: ..." and exits 1.
# Works on a copy of darling.toml/schema/generator in a temp dir; the tree and the build dir are not touched.
HERE=$(dirname "$0")
T=$(mktemp -d "${TMPDIR:-/tmp}/gen-schema-test.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
cp "$HERE/darling.toml" "$HERE/darling.schema.json" "$HERE/gen-from-schema.py" "$T/" || exit 2
printf '\n[run.series.quotetest]\nprefix = "quotetest"\nruns = 1\ntimeout = 120\nchrome_extra_args = "--lang=%s"\n' "en'US" >>"$T/darling.toml"
out=$(python3 "$T/gen-from-schema.py" --check --out "$T"); rc=$?
case "$out" in "SCHEMA-DRIFT FAIL schema:"*chrome_extra_args*) [ "$rc" = 1 ] && { echo "GEN-SCHEMA-TEST pass"; exit 0; };; esac
echo "GEN-SCHEMA-TEST fail rc=$rc out=$out"; exit 1
