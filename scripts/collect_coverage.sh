#!/usr/bin/env bash
# Run inside the CI image with source at /input and a writable /reports mount.
set -euo pipefail

source_dir=$(mktemp -d /tmp/j1939-coverage.XXXXXX)
trap 'rm -rf "$source_dir"' EXIT
# Discard existing build data so stale gcda/gcno files cannot affect the result.
tar -C /input --exclude=.git --exclude=build --exclude=build_cov \
    --exclude=coverage-reports -cf - . | tar -C "$source_dir" -xf -
cmake -S "$source_dir" -B "$source_dir/build_cov" -DENABLE_COVERAGE=ON \
    -DCMAKE_C_FLAGS='--coverage -O0 -g'
cmake --build "$source_dir/build_cov" --target all_tests -j2
"$source_dir/build_cov/bin/all_tests"

cd "$source_dir"
gcovr --root "$source_dir" "$source_dir/build_cov" \
    --exclude '.*_tests?\.cpp$' --exclude 'tests/' \
    --exclude '(.*/)?CMakeFiles/' --exclude-throw-branches \
    --print-summary --json-summary /reports/summary.json \
    --html-details /reports/coverage.html --txt /reports/coverage.txt
