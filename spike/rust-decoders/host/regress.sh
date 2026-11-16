#!/usr/bin/env bash
# What the Rust decoders do with the fuzz regression inputs (crashes found in the C decoders):
# error (clean RDEC_ERR return), panic (abort), hang (10 s timeout) or decoded. Host run.
set -uo pipefail
cd "$(dirname "$0")"
for f in ../../../test/fuzz/image/corpus/regress_*; do
  timeout 10 build/compare --rust-only "$f" >/dev/null 2>&1
  rc=$?
  case $rc in 0) o=decoded ;; 2) o=error ;; 124) o=hang ;; 134) o=panic ;; *) o="exit $rc" ;; esac
  echo "$(basename "$f") $o"
done
