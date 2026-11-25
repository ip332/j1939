#!/usr/bin/env bash
# SocketCAN-free host validation. C++17 is needed by recent GoogleTest releases;
# generated accessors are separately compiled by the tests as C11 and C++14.
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="${J1939_HOST_BUILD_DIR:-/tmp/j1939-quarry-host}"
mkdir -p "$build_dir"
read -r -a host_flags <<< "${J1939_HOST_CXXFLAGS:-}"
gtest_text="${J1939_GTEST_FLAGS:-}"
if [[ "$gtest_text" =~ [^[:space:]] ]]; then
    read -r -a gtest_flags <<< "$gtest_text"
else
    # Unset, empty and whitespace-only all select the normal link defaults.
    gtest_flags=(-lgtest -lgtest_main -pthread)
fi
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    -I j1939_parser -c j1939_parser/j1939_db.c -o "$build_dir/db.o"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    ${host_flags[@]+"${host_flags[@]}"} -I j1939_parser -I j1939_parser/dbc_parser -I can_reader \
    "-DDBC_FOLDER=\"$PWD/tests/dbc/\"" "-DJ1939_INCLUDE_DIR=\"$PWD/j1939_parser\"" \
    j1939_parser/dbc_parser/*.cpp \
    j1939_parser/j1939_frame.cpp j1939_parser/j1939_parser.cpp \
    j1939_parser/j1939_db_tests.cpp j1939_parser/j1939_parser_test.cpp \
    j1939_parser/j1939_codec_tests.cpp tests/j1939_db_roundtrip_tests.cpp \
    can_reader/{long_can_message,fast_packet_message,transport_protocol}.cpp \
    can_reader/{long_can_message,fast_packet_message,transport_protocol,transport_protocol_known_issues}_tests.cpp \
    "$build_dir/db.o" ${gtest_flags[@]+"${gtest_flags[@]}"} -o "$build_dir/tests"
"$build_dir/tests" "$@"
