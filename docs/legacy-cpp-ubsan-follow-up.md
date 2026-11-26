# Separate legacy C++ UBSan follow-up

These seven diagnostics were observed in a wider host run with recovering UBSan.
The run reported passing GoogleTest assertions, but was not sanitizer-clean.
There is no baseline run establishing when these defects were introduced. They
are outside the generated-C observed-PGN producer and are not fixed by it.

| Location | Observed trigger |
| --- | --- |
| `j1939_parser/j1939_parser.cpp:218` | Scale-1/offset-0 negative floating value converted to `uint64_t` in `setSignalValue` |
| `j1939_parser/j1939_parser.cpp:220` | Negative scaled floating value converted to `uint64_t` |
| `j1939_parser/j1939_frame.cpp:194` | Signed subtraction overflow in `SPN::toDouble` |
| `j1939_parser/j1939_codec_tests.cpp:94,106,118,131` | Test expected-value expressions shift by 64 |

The writer sites were at lines 214 and 216 in the original reviewed tree
(`637878e`); merging the C++ mux fixes from main moved them to 218 and 220.
The other diagnostic locations are unchanged.

Reproduce separately from the focused C decoder/model tests:

```sh
# macOS reproduction used these paths/warning exceptions; adapt for other hosts.
J1939_HOST_BUILD_DIR=/tmp/j1939-host-ubsan \
J1939_HOST_CXXFLAGS='-I/usr/local/include -Wno-vla-cxx-extension -Wno-deprecated-declarations -Wno-sign-compare -Wno-missing-field-initializers' \
J1939_GTEST_FLAGS='/usr/local/lib/libgtest.a /usr/local/lib/libgtest_main.a -pthread' \
  bash scripts/quarry_host_tests.sh --gtest_filter='DbcCGenTest.*:DbcModelTest.*'

# Recover to inventory all diagnostics. A zero test exit does not make this clean.
ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/j1939-host-ubsan/tests

# For the follow-up's acceptance gate, fail on the first UB instead.
ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /tmp/j1939-host-ubsan/tests
```

On toolchains requiring it, supply `J1939_HOST_CXXFLAGS` for GoogleTest headers,
legacy warning exceptions and `J1939_GTEST_FLAGS` for its libraries. See the host
script for defaults. The separate repair should define representable signed
encoding and safe full-width masks, keep independent expected values, and rerun
the signed writer/sign-extension cases with fail-fast sanitizers. Seven disabled
transport regressions remain outside this follow-up too.
