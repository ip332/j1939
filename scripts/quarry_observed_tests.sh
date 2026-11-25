#!/usr/bin/env bash
# Permanent capture-independent tests. Build Quarry's compiler first; no capture needed.
set -euo pipefail
cd "$(dirname "$0")/.."
export PYTHONDONTWRITEBYTECODE=1
python3 examples/quarry_observed/test_expand.py
tools=()
if [[ -n "${J1939_OBSERVED_TOOLS_FROM:-}" ]]; then
  tools=(--tools-from "$J1939_OBSERVED_TOOLS_FROM")
fi
python3 examples/quarry_observed/expand.py --synthetic --sanitize \
  --output "${J1939_OBSERVED_TEST_BUILD:-/tmp/j1939-observed-synthetic-tests}" \
  --quarry-build "${QUARRY_BUILD:-../quarry/build/debug}" ${tools[@]+"${tools[@]}"}
