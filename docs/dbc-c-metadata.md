# C metadata foundation for signal subscriptions (issue 22)

Status: experimental metadata-v1 implementation; review before publishing this
new extension as a stable SDK contract. No subscription runtime, BRF change, stable
signal-ID contract or four-language subscription API is introduced. The DBC
remains the source of CAN bit layouts. This describes the opt-in metadata-v1
extension and the unchanged legacy compact database separately.

## Inventory

The strict host model's supported input subset is specified in
[dbc-model-v1.md](dbc-model-v1.md). A legacy parse accepting a declaration is
not proof that its semantics were preserved.

| Information | Parsed DBC/model | Compact C database | Metadata-v1 extension |
| --- | --- | --- | --- |
| Original BO_ ID, priority, SA and PDU1 destination bits | Host model keeps original ID and extended flag | Canonical ID/PGN; priority/destination masked; generic or exact-SA rows | Every original ID, original name, and its decode-row index |
| Source applicability | Declared addresses and explicit J1939Default; not dynamic NAME association | sa_mask/sa and generated generic-row policy | Keeps declarations/default presence separately from inherited decode routing |
| Message aliases | Original definitions retained in model | One representative name chosen on collapse | Every original message and signal name retained, referring to canonical rows |
| Signal name, bit position, width, byte order, signedness | Parsed and retained | Retained; names optional | Reuses core fields, no layout duplication |
| Scale/offset/ranges | Numeric values and exact decimal strings | Numeric double constants only | Exact strings beside existing numeric constants |
| Units | Complete quoted text, including spaces and explicit empty string | Parsed but lost | NUL-terminated unquoted text in shared string pool |
| Enums | uint64 raw patterns and labels | Values and optional labels retained | Reuses core enum tables; no quality inference |
| Simple mux switch/branch and raw value | Parsed, retained | Flags and mux value retained | Reuses core; references remain valid after sorting |
| SPN | Presence distinct from explicit zero in model | Zero conflates absent/explicit zero; representative SPNs/index | Original message/signal SPN plus presence flag |
| Explicit default marker | Presence and true/false retained in model | Affects collapse but declaration lost | Presence and boolean retained on each original message |
| Provenance | Logical source basename, bytes, FNV-1a checksum, declaration locations | Lost | Source table and message/signal lines |
| VERSION | Optional exact text | Lost | Source version offset, sentinel when absent |
| Nodes, NS_ symbols, transmitter/receiver names | Retained by strict host model | Lost | Not included in this increment; use host model when needed |
| Quality, timestamps, units conversion, schema/configuration identity | Not established by DBC/model | Not established | Not invented |

Float-wire values, extended/nested mux, CAN FD and transport payloads, comments,
custom attributes and other strict-model-unsupported dialect constructs reject
in the metadata profile. They are not silently copied into integer semantics.
The legacy parser can retain some text that the strict model does not accept;
escaped/non-ASCII unit text is outside this metadata profile and rejects explicitly.
Standalone accessors contain layout in generated operations, raw/physical
getters and enum defines; they do not provide discoverable unit/provenance or
subscription metadata. Their full-DLC buffer preconditions remain unchanged.

## Representation and use

```
dbc_compiler -in input.dbc -name vehicle -c_path vehicle.c -c-metadata
```

Compile the generated file and `j1939_db.c` as before. Include its generated
header, which declares both `vehicle_db` and `vehicle_metadata`.

```c
const j1939_db_metadata_t *meta = &vehicle_metadata;
uint16_t index = 0; /* derived from a checked message/signal lookup */
const char *unit = j1939_signal_unit(meta, index);
const char *factor = j1939_metadata_text(meta, meta->signals[index].scale_off);
/* Numeric conversion still uses j1939_signal_phys(&meta->db->signals[index], raw). */
```

`j1939_db_metadata.h` adds separately versioned const tables, linked to the
existing `j1939_db_t`. They live in the same generated translation unit and
share its one interned string pool. The extension does not reproduce widths,
byte orders, flags, numeric constants or enum tables. Original-definition tables
are necessary because canonical decode rows deliberately combine aliases; they
are not a second independent catalog or parser.

Signal metadata is indexed exactly like `db->signals`. Original message entries
refer to a decode row, source and signal-definition slice. Original signal
entries refer to a global decode-signal index. SPN/default presence belongs to
original definitions, not a guessed canonical representative. Original priority,
SA and destination can be recovered from the original ID with existing ID logic.

All pointers borrow immutable generated storage for its entire linked/module
lifetime. Units are not allocated or parsed at runtime. A non-NULL pointer to
`""` means explicitly empty DBC unit text; NULL from helpers means absent extension,
invalid index/version or unavailable storage. Strict metadata generation requires
quoted units, so missing/malformed units reject rather than become empty text.
`J1939_NO_NAME` also marks absent VERSION; explicit empty VERSION has a valid
empty-string offset. Units are labels, not an automatic unit-conversion policy.

Exact decimals preserve leading zeros, signs, exponents and trailing zeros.
They are descriptive strings, never runtime numeric input. Numeric constants keep
existing double semantics: physical values may round at 64-bit boundaries and
need not equal exact decimal arithmetic. Ranges and enum labels do not establish
validity/NA/error status. No clock or quality data is synthesized.

The decoder's legacy collapse compares numeric layouts and enums, not units or
exact decimal text. Metadata generation rejects a collapse whose units or any
of the four exact decimal strings differ between original and canonical signals.
Separate numerically different source-specific rows keep their own metadata.
For a rejected same-layout conflict, generate separate databases and select an
explicit interpretation, or review a future interpretation-aware routing design.
Do not silently take the first unit or normalize away exact spelling.

Generic decode rows remain the existing compiler policy, not proof that every
source uses that definition. A subscription generator must review source
applicability using original definitions and explicit application routing policy.
No runtime SA/NAME tracking is supplied. Original definitions also retain SPNs and
names that are intentionally excluded from layout equality.

## Compatibility and profiles

No fields were added to `j1939_signal_t`, `j1939_message_t` or `j1939_db_t`.
Existing initializers, compiled target decoder ABI and generated database symbols
remain usable. A committed-baseline compiler reproduced a 65536-signal stripped
database with wrapped uint16 counts and invalid generated C. The shared bounds
check now rejects it before output, including when there is no name index or
following message. Valid existing databases are unchanged. The old four-argument
host emitter is retained as an overload.
The new overload takes an explicit metadata boolean and requires a successful,
unnormalized strict-model session. Host tools still allocate and must rebuild
when source code changes; there is no installed C++ compiler ABI promise.

For valid inputs under the CLI's normal locale, generated C/H without
`-c-metadata` remain byte-identical, including `-no-strings`.
The compact profile can omit optional descriptive metadata without changing
raw decoding or numeric physical conversion. It cannot supply faithful unit or
original-definition lookup and must not claim a complete subscription catalog.
The full metadata profile requires strings and is incompatible with `-no-strings`
and `-model_path`. This increment deliberately supplies two profiles, not an
untested collection of independent metadata switches.

The metadata CLI honors `-name` before C emission; its default is `j1939` when
no name is supplied by the strict model parser. Use an explicit name for multiple
linked databases and deterministic multi-input generation. The legacy CLI's late
`-name` handling for C emission is unchanged and remains a separate historical
limitation. Input order does not affect metadata when the symbol name and logical
filenames/content are fixed. All emitted numeric streams use the classic locale,
including when the embedding host uses grouped/comma numeric formatting. It does
not promise stable indices across edits.

Metadata header/generator versions must match (compile-time rejection), and the
helpers reject unknown runtime metadata versions. This is native compiler/target
structure layout, not a persisted wire ABI. A future incompatible extension needs
a new version/type contract; do not append fields and assume old objects match.
Target linkers/flash placement and pointer relocation still require platform checks.
The full profile additionally requires `j1939_db_metadata.h` when compiling the
generated files; distribute it alongside `j1939_db.h`/`j1939_db.c`. This is still a
source-tree workflow, not a newly installed runtime SDK. No mandatory heap, string
parsing, runtime hashing or runtime DBC parser is added.

Strict validation and metadata construction complete before either output stream
is touched. Unsupported input preserves existing files. Stream/write failures are
reported, but multi-file I/O after validation is not atomic. Existing legacy-only
parser behavior remains unchanged; metadata parse rejection uses exit status 4.

## Identity, provenance and future subscriptions

The source checksum is the host model's noncryptographic FNV-1a64 over exact
bytes, NOT SHA-256, a schema ID or authentication. Source names/lines and original
IDs identify declarations within this generated bundle; array indices are not
portable signal IDs. Explicit message/signal SPN 0 remains distinguishable.

This database plus extension can be the C subscription metadata foundation;
a second copy of layouts/numeric metadata is unnecessary. Future generators
can derive C/C++/Python/Go names and interpretation keys from the same strict
model and original-definition graph. Runtime-only embedded consumers need not
load protobuf or Quarry. Before a public subscription API, decide stable/scoped
IDs, interpretation-aware routing, optional profiles and packaging together.

Quarry mapping/configuration keys still require their existing checks. A BRF
record ID or these native table addresses do not prove the DBC interpretation.
Direct CAN frames do not authenticate their interpretation either. Keeping or
rewriting hashes cannot authenticate a party able to rewrite the data and hashes.
The frozen host model and BRF are unchanged.

## Validation and measured footprint

Permanent tests: `dbc_metadata_tests.cpp`, `tests/dbc_metadata_cli_tests.py` and
`tests/dbc/metadata_synthetic.dbc` (hand-authored under the project license).
The model contract script also runs the metadata CLI tests. A synthetic
uint16-count boundary regression additionally checks stripped decoder output
rejection, both with and without a following message. Host filters should
include `DbcMetadataTest.*`; old database/codec and integration tests remain gates.

The small fixture contains three original messages and eight original signals,
canonicalized into two decode rows and seven signals. It covers signed/unsigned,
Motorola, simple mux, leading-zero/exponent decimals, empty/spaced units, enums,
aliases, explicit zero SPN and explicit false default. Tests compile and run C11
and C++14 with fail-fast ASan/UBSan, check metadata/source association, exact text,
numeric behavior, deterministic source-order generation and lossy-collapse errors.

Measured with Clang `-Os` and function/data sections, same symbol name and inputs:

| Object/profile | Compact | Full metadata | Increase |
| --- | ---: | ---: | ---: |
| x86-64 macOS loaded const sections | 720 B | 1368 B | 648 B |
| Cortex-M4 Thumb cross-compiled .rodata section sums | 656 B | 1258 B | 602 B |

The ARM increase is 416 bytes of extension structures plus 186 string bytes;
existing layouts/numeric/enum data are unchanged. Per-entry ARM sizes are 20 B
per canonical signal, 16 B per original signal, 28 B per original message, 24 B
per source and 40 B for the extension root, plus shared strings. The existing
`-no-strings` profile measured 516 B of ARM .rodata on this fixture. Object headers, debug/comments,
attributes, relocation/symbol tables, final link alignment and dead stripping are
excluded from the ARM .rodata sum. Generated objects contain zero .text and no
writable data/bss. Link/load relocation and real firmware memory placement are
not measured RAM claims. The measured generated data objects do not call the
inline query helpers; calling helpers can add instructions in application code.
There is no target execution, RTOS validation or speed
measurement. Host tests prove neither target throughput nor safety certification.

Reproduce outside the repository with a built compiler and explicit paths:

```sh
$COMPILER -in tests/dbc/metadata_synthetic.dbc -c_path /tmp/compact.c
$COMPILER -in tests/dbc/metadata_synthetic.dbc -name metadata_synthetic \
  -c_path /tmp/full.c -c-metadata
clang --target=arm-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding \
  -std=c11 -Os -ffunction-sections -fdata-sections -I j1939_parser \
  -c /tmp/full.c -o /tmp/full.o
# Repeat for compact.c, and sum .rodata sections with llvm-size -A.
```
