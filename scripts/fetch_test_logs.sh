#!/usr/bin/env bash
#
# Fetches canboat's public NMEA 2000 sample logs (samples/ only, ~216 MB) for the tests that
# replay full captures. Those tests skip when the files are missing; the always-on tests use
# the small excerpts committed under testdata/canboat/ instead.
#
# The checkout is pinned to CANBOAT_SHA so the expected numbers in the tests stay valid.
# Bump it only together with those numbers.
#
# Usage: scripts/fetch_test_logs.sh [destination]   (default: testdata/external/canboat)
#
set -euo pipefail

CANBOAT_URL="https://github.com/canboat/canboat.git"
CANBOAT_SHA="ee93a6b9dc2aa6e939e4e95bc925e4841dad98e7"   # master, 2026-11-09

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/testdata/external/canboat}"

if [ -e "$DEST" ] && [ ! -d "$DEST/.git" ]; then
    echo "error: $DEST exists but is not a git checkout; remove it or pass another path" >&2
    exit 1
fi

if [ ! -d "$DEST/.git" ]; then
    mkdir -p "$DEST"
    git -C "$DEST" init -q
    git -C "$DEST" remote add origin "$CANBOAT_URL"
fi
# Non-cone sparse checkout of just samples/ and the license. (Cone mode would also bring in
# every top-level file, including canboat's own CLAUDE.md/AGENTS.md agent instructions,
# which have no business sitting inside this repo's tree.) Re-applied on every run so older
# checkouts are narrowed too.
git -C "$DEST" sparse-checkout set --no-cone '/samples/' '/LICENSE'

if [ "$(git -C "$DEST" rev-parse -q --verify HEAD 2>/dev/null || true)" = "$CANBOAT_SHA" ]; then
    echo "canboat samples already at $CANBOAT_SHA in $DEST"
    exit 0
fi

# Shallow, blob-less fetch of exactly the pinned commit (GitHub allows fetching by SHA), so
# neither history nor files outside samples/ are downloaded.
git -C "$DEST" fetch -q --depth 1 --filter=blob:none origin "$CANBOAT_SHA"
git -C "$DEST" checkout -q --detach "$CANBOAT_SHA"
echo "canboat samples at $CANBOAT_SHA in $DEST/samples"
