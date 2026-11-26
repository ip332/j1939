#!/usr/bin/env bash
# Host exporter + real protobuf consumer contract. Requires protoc and Python protobuf.
# J1939 itself has no protobuf build/runtime dependency.
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="${J1939_MODEL_BUILD_DIR:-/tmp/j1939-model-contract}"
mkdir -p "$build_dir"
read -r -a host_flags <<< "${J1939_MODEL_CXXFLAGS:-}"
sources=()
for source in j1939_parser/dbc_parser/*.cpp; do
    case "$source" in *_tests.cpp|*_test.cpp) ;; *) sources+=("$source");; esac
done
c++ -std=c++14 -Wall -Wextra -Werror ${host_flags[@]+"${host_flags[@]}"} \
    -I options -I j1939_parser -I j1939_parser/dbc_parser \
    dbc_compiler/dbc_compiler.cpp "${sources[@]}" j1939_parser/j1939_frame.cpp \
    -o "$build_dir/dbc_compiler"
protoc -I schema --python_out="$build_dir" schema/dbc_model_v1.proto
PYTHONPATH="$build_dir${PYTHONPATH:+:$PYTHONPATH}" \
    python3 tests/dbc_model_consumer_tests.py "$build_dir/dbc_compiler"
python3 tests/dbc_metadata_cli_tests.py "$build_dir/dbc_compiler"
