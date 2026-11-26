# Portable DBC decoding prerequisites

`dbc_compiler -in input.dbc -c_path database.c` emits const plain-C database
storage and its header. Compile that storage with `j1939_parser/j1939_db.c` as
C11. No runtime DBC parser, constructors, or codec heap allocation is required.
`j1939_signal_raw` needs `dlc` readable payload bytes and trusted generated
metadata, rejects signals outside that length, leaves its output unchanged on
failure, and does not select mux activity. Call `j1939_signal_active` separately.
Raw integers preserve bit patterns; `j1939_signal_phys` interprets signedness and
applies binary64 scale/offset, without assigning unavailable/error semantics.

`-accessor_path payloads.h` emits standalone C11/C++14 byte-mask/shift accessors.
There is no runtime library dependency. Callers provide the full message-DLC
buffer and select the active mux alternative. Raw stores preserve other bits;
physical getters apply signedness/scale/offset. Byte-aligned Intel signals wider
than 64 bits use byte-copy accessors, not scalar telemetry mappings.

Both portable paths support Intel/Motorola integer fields through 64 bits and
one unsigned nonnested mux switch. Extended mux ranges, nested switches, and
floating wire formats reject. The generated-C database and accessors have
separate length preconditions; passing raw extraction's checked short-buffer
contract does not authorize a short buffer for unchecked standalone accessors.

The strict host model is documented in [dbc-model-v1.md](dbc-model-v1.md).
The host model/projection allocate; they are generation tools, not target code.
The legacy packed union is compiler/ABI dependent. C++ PGN maps allocate and are
not the portable C runtime. Host parser code still contains a C++ VLA extension.
No RTOS, embedded memory/timing, safety certification or target compiler
validation is established by host tests.

The focused host gate is `scripts/quarry_host_tests.sh` with filter
`DbcCGenTest.*:DbcModelTest.*`; it compiles and executes independent vectors through
the generated C database/accessors, including both byte orders, cross-byte signed
values, full-width integers, mux inactivity, short frames and rejected constructs.


Legacy `parseFile` continues to accept/ignore `SIG_VALTYPE_` and `SG_MUL_VAL_`
as before; portable generation records their presence and fails before emitting
any output. Strict model mode rejects them at parse time. A successful legacy
parse does not authorize portable integer interpretation of those declarations.
Out-of-range, overflowing, malformed and nested mux markers likewise cannot
become unconditional portable signals. Accessor enum names are deterministically
disambiguated against the entire emitted API. C numeric emission preserves DBC
decimal meaning, including integer-form factors with leading zeroes; original
model metadata still retains exact source tokens. Failed accessor streams and
CLI writes return failure.


The CLI validates all requested portable artifacts before opening any output,
including when combined with legacy PGN/union requests. A portable validation
failure preserves existing C, header, accessor and legacy files. This is not a
multi-file atomic-write guarantee: I/O errors or failures in legacy packed-union
generation can still leave partial outputs. Invalid scale/offset/minimum/maximum
tokens are rejected by the C backend, never replaced with a parsed numeric prefix.
Whitespace-only `J1939_GTEST_FLAGS` selects the same defaults as unset/empty;
explicit nonempty flags still select the caller's link configuration.
