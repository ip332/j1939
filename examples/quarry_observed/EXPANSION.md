# Observed-PGN integration prototype

`expand.py` extends the earlier `run.py` experiment. This document deliberately
contains no private message names or capture statistics. The earlier detailed
report was retained privately during review; its publication needs an explicit
content decision. Generated metadata, source comments, payloads, context, logs,
and coverage reports can contain proprietary material. Keep them outside both
repositories. No publication or commit is part of this work.

## Compile and identify the interpretation

The host projection uses J1939's existing DbcParser, its read-only source model
view, and its generated const C databases. Each DBC is parsed separately so source
conflicts survive. This is a source-tree tool, not a stable installed C++ ABI or
an implementation of the general model exporter. Projection pins C numeric locale
for `std::stod` and classic C++ stream formatting. Exact decimal and unit literals,
enums, SPN presence (including zero), signedness, byte order, mux metadata, source
hashes, original IDs, aliases, and locations remain in `mapping.json`.

Every scalar telemetry BRD is imported into one `observed.brd` configuration.
Quarry supports this through its YAML configuration frontend; see its
`docs/compiler-architecture.md` and `docs/layout-algorithm.md`. C, owning C++, and
reflective QBS are compiled from that same configuration. Regeneration with
reversed imports must produce identical code and QBS; generated record IDs must
be unique. IDs number sorted namespaces and sorted local names. They are stable
for the same record set, not durable global identities: inserting a namespace can
renumber records. Quarry's structural schema identity also cannot prove exact DBC
scales, units, bit layouts, or source applicability.

The prototype mapping contract is version **3** (older bundles reject). Each
record's test/storage envelope contains, in order:

1. 32 bytes: full SHA-256 mapping key, including exact DBC signal interpretation,
   source content hash, PGN, routing policy, and applicable source addresses.
2. 32 bytes: configuration contract SHA-256, covering reflective QBS SHA-256 and
   the complete ordered mapping-key/record-ID association.
3. Unchanged BRF bytes emitted by Quarry-generated C.

The TSV harness represents these bytes as hex alongside the expected mapping key.
The C++ consumer checks both digests against its compiled expectations **before
calling the generated field decoder**. Record-ID validation provides an additional
configuration-local check. Every ordered pair of different mappings is tested for
wrong-envelope rejection and, after relabelling the envelope, wrong-record-ID
rejection. Identical scalar shapes and unit/scale-only differences are included.
A changed configuration digest also rejects before field decoding.

Digests are computed during host generation and compiled as constants; the C
producer does not need a runtime SHA library or runtime metadata parsing.

This envelope is integration glue, not a BRF change or an authenticated protocol.
Direct use of generated `decode_*` functions bypasses the envelope checks. The
example consumer does not itself load or verify the manifest; bundle verification
must be performed before using its stored context/stream associations. A caller
can rewrite compatible BRF record IDs and envelope digests to make another
interpretation decode. A party able to rewrite data and accompanying hashes can
forge associations. No standalone editable payload hash would authenticate this. A future
production framing/identity contract remains a separate design decision.

## Context, routing, and completeness

`manifest.json` pins generated sources, BRDs, QBS, mapping metadata, adapter
binaries, the record stream, `context.json`, and the reported `coverage.json`.
Changing coverage counts is detected by bundle verification; this is consistency
of the reported file, not independent proof that the tests actually ran. Context retains frame ordinal,
ID, source/destination, original payload, and the available timestamp/bus/status/
flag fields. Context order and record-stream order are bound by their manifest
hashes; swapping context rows rejects. Validate the bundle before using sidecar
associations. This remains file-level association, not a self-contained network
frame carrying timestamps. Digests provide consistency, not authenticity.

Timestamps retain original integer secs/nsecs; epoch, clock accuracy, and absent
synthetic timestamps are not invented. Every context row explicitly marks quality
`unknown`. No reserved/all-ones/error pattern or enum label is assigned validity
semantics. One record per observed frame is an explicit prototype policy; no
batching prerequisite is imposed.

Full ordered signal metadata determines equivalence. Conflicts require a unique
exact source-address match. Supplemental proprietary definitions require a
configured source address. The reviewed ISO Request primary-default policy is
explicit: one unsigned Intel 24-bit field at bit zero, DLC 3, scale 1/offset 0.
All candidate signal interpretation metadata must match, except field name and
source line. Mux, enum, unit, range, or SPN differences reject. This policy is not
inferred from names or arbitrary DBC defaults and executes no protocol request.

A capture run with **any** skipped frame exits nonzero after writing inventory.
Synthetic mode instead enforces an exact expected skip-reason multiset. Unknown,
ambiguous, standard, short, FD/oversized, and transport inputs cannot produce a
silently incomplete successful capture report. No individual signal is omitted
to make a schema fit. Float-wire, extended/nested mux, and unreviewed unsupported
DBC dialect constructs reject explicitly. Reassembly and dynamic address/NAME
resolution remain separate work.

The C database's canonical-row equivalence uses decoded numeric values and does
not preserve all name/unit distinctions used by the mapping signature. Explicit
source routing, the checked mapping identity, and the independent extraction
oracle address this for exercised frames; passing a capture is not a proof for
all possible alias layouts or decimal spellings.

## What validation proves

The producer is **generated Quarry C**, J1939's portable C decoder and const C
database tables. It uses caller-owned structs and a checked 4096-byte BRF buffer;
extraction/encoding does not allocate or parse DBC at runtime. The digest envelope
is streamed by the host harness. Host stdio and sanitizer machinery can allocate.
The independent consumer is **owning C++**, uses vectors/strings, and links no
J1939 parser or bit decoder. Python independently extracts Intel/Motorola raw bits,
sign-extends with arbitrary-precision integers, and checks active mux branches.

`raw` counts exact integers round-tripped through BRF. `physical_arithmetic`
counts the producer's separate `j1939_signal_phys` calculation versus 96-digit
Decimal raw × scale + offset, with an explicit binary64 rounding bound. Physical
values are not encoded in BRF or independently measured engineering observations.
Raw values above 2^53 remain exact integers; binary64 physical values may round.
Inactive mux fields are absent. Numeric arithmetic does not validate quality.

Negative subprocess checks require a specific normal exit status and clean
stderr. Crashes, sanitizer diagnostics, and unrelated failures cannot count as
successful rejection. Tests cover short prefixes, malformed BRF, source/PGN
mismatches (including PGN zero), wrong mapping/configuration, all cross-schema
pairs, artifact/context corruption, and unknown contract versions.

The redistributable fixtures are hand-authored: cross-byte Intel/Motorola,
signed/unsigned 64-bit extremes, enums, explicit SPN zero, mux branches/inactive
signals, source-specific scale and unit differences, PGN zero, and ISO Request
from multiple source/destination addresses. Private capture data is not a test
fixture. A capture recount validates inventory only; a fresh full encode and oracle
comparison is a separate gate. Detailed capture results stay in private output.

## Reproduce

Build Quarry's schema compiler first. Run from the J1939 root, using a fresh
output directory after changing inputs or runner code:

```sh
QUARRY_BUILD="$QUARRY_BUILD" \
J1939_OBSERVED_TEST_BUILD=/tmp/j1939-observed-synthetic \
ASAN_OPTIONS=abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
/bin/bash scripts/quarry_observed_tests.sh

python3 examples/quarry_observed/expand.py "$CAPTURE" \
  --dbc "$PRIMARY_DBC" --supplemental-dbc "$SUPPLEMENTAL_DBC" \
  --private-profile "$REVIEWED_INPUT_PROFILE" \
  --capture-reader-sources "$CAPTURE_READER_SOURCES" \
  --output /tmp/j1939-observed-capture --quarry-build "$QUARRY_BUILD" --sanitize
```

`J1939_OBSERVED_TOOLS_FROM` optionally reuses fingerprint-checked host tools;
the script also works with it unset on Bash 3.2. Full capture replay additionally
requires explicit DBC paths, an out-of-tree reviewed-input profile, and external
legacy RosStream sources. No private filename, capture fingerprint, reader path,
or supplemental DBC is discovered by default. Repeat `--supplemental-dbc` in the
intended source-precedence order. The profile is JSON with
`reviewed_dbc_sha256` (an array of reviewed lowercase SHA-256 input fingerprints)
and `reviewed_capture_sha256` (one fingerprint). It selects previously reviewed
legacy parser/reader inputs; it is neither authentication nor an input review.
Keep profiles outside the repository. Do not auto-approve an unreviewed capture
by calculating a fingerprint. Without a profile, synthetic mode uses only the
strict parser and redistributable fixtures. The legacy reader is gated to the reviewed capture content hash and
cross-checked frame-by-frame against a safe Ruby/Psych conversion. Other captures
require reader hardening/review; this is not a general ROS reader SDK.

The integration does not modify Quarry. Use a separate committed-source Quarry
checkout for reproducibility; set `QUARRY_SOURCE` and `QUARRY_BUILD` explicitly
when its source/build are not in the default sibling layout. The harness compiles
that source tree's C runtime. These schemas contain fixed scalars and do not need
array batching. Separate batching work in an existing workspace is not a
prerequisite and is not validated by these tests. Earlier
`README.md`/`STORAGE.md` results are historical experiments, not current gates.

Validation is host macOS/AppleClang with ASan/UBSan on projection, C decoder,
generated C producer, C runtime, and C++ consumer. The prebuilt schema compiler
and metadata-only header inventory tool are not sanitizer-instrumented. Linux,
Windows, SocketCAN, RTOS, LeakSanitizer, embedded RAM/flash, and target timing are
not validated. The legacy parser still contains a C++ VLA extension. Production
framing, installed-package generation, general dialect support, transport, and
resource/timing validation remain focused follow-ups.

## Review-fix validation (2026-11-24)

On macOS, AppleClang 17 and Python 3.14.7:

- Python policy/oracle tests: 18 passed, including coverage-report tamper detection
  and reviewed-profile validation.
- Bash 3.2.57 script with tools cache unset and set: the final fixture passed: 8 schemas, 20 observations, 14 supported,
  exactly 6 expected skips, 13 defined signals, 32 exact raw comparisons,
  32 physical-arithmetic comparisons, and 7 inactive mux fields.
- Final synthetic ASan/UBSan negatives: 59 short frames, 16 malformed BRFs,
  3 wrong sources, 8 wrong PGNs, 2 unknown keys, 56 cross-mapping pairs,
  56 relabelled wrong-record-ID pairs, 8 configuration-digest mismatches,
  and one each of QBS, mapping, context-order, and version rejection.
- A separate configuration-addition probe renumbered all six original test
  records when a namespace sorting before them was inserted. Stable import
  reordering is enforced on every generation; record-set evolution is not stable.
- Focused `DbcCGenTest.*:DbcModelTest.*`: **34/34** passed on Clang and
  GCC 16 with fail-fast ASan/UBSan. Numeric/accessor regressions compile and run
  as strict C11 and C++14 against independent literal expectations.
- Protobuf/CLI contract: **10/10** on Clang and GCC, including legacy PGN/union
  compatibility versus portable/model rejection of unsupported wire declarations.
  Bash 3.2 optional-array paths were exercised unset and set, with toolchain
  wrappers supplying this host's include paths/warning exceptions when unset.
- Six isolated legacy cases reproduce seven UBSan diagnostics under recovery:
  negative conversions in `j1939_parser.cpp:214,216`, signed overflow in
  `j1939_frame.cpp:194`, and shifts by 64 in
  `j1939_codec_tests.cpp:94,106,118,131`. That run is **not sanitizer-clean**;
  no full-host sanitizer-clean claim or baseline attribution is made. See
  `../../docs/legacy-cpp-ubsan-follow-up.md` for the separate follow-up.

The generated producer requires C11; the host projection uses C++14 and the
independent owning consumer C++20. Compiler extensions in legacy host parser code
prevent claiming strict portable C++14 for the entire host tool.

The complete synthetic producer/runtime/consumer pipeline also passed on GCC 16
and Clang with fail-fast ASan/UBSan. BRF bytes, raw decoded output and physical
arithmetic output matched byte-for-byte across compilers. A sanitized in-process locale probe entered projection
with `fr_FR.UTF-8` comma-decimal numeric locale: projection JSON and generated
C/H matched the C-locale baseline. The private probe renamed `main` and added an
explicit return to preserve C++'s special implicit-main-return semantics; an
initial probe without that return itself triggered UBSan and was corrected.
This probe failure was in the test harness, not evidence of a production locale
failure.

Generation currently needs Quarry's source `include/`, `runtime_c/typed_record.c`,
and the build's generated version headers, in addition to the prebuilt schema
compiler. Capture mode also needs external legacy reader sources; synthetic
mode does not. These are explicit source-tree-only dependencies, not installed
SDK guarantees. A real capture run deliberately paired with an incomplete DBC
selection was also tested: exit 1, inventory retained, no producer generated,
and no PASS line.

A fresh full reviewed-capture encode under fail-fast ASan/UBSan passed with no
skipped frames, independently checked raw values, all cross-schema negatives,
and final bundle integrity. A separately implemented capture recount agreed;
it was not used as a substitute for encoding. The final manifest also rejected
record-stream reordering. Detailed capture statistics, per-PGN coverage, exact
local commands, and logs remain in private scratch output rather than this
publication-neutral document.


## Build selection and diagnostic reports

The runner prints `QUARRY_PROVENANCE` and records the same data in the manifest:
selected source path/revision, tracked dirty paths, C runtime/C++ backend hashes,
compiler path/hash, build path and its recorded CMake source root. A dirty sibling
checkout is not attributed to committed Quarry. These are selected-input facts,
not authenticated build attestation; rebuild the compiler from the intended
committed source and match build/source paths for a baseline run.

`available-dbcs.json` is a derived **unverified diagnostic**, outside the manifest.
Its header-only inventory is not decoding/coverage evidence and editing it is not
expected to fail bundle verification. This differs from manifest-bound
`coverage.json`, `context.json` and the encoded record stream.


Mixed legacy/portable CLI requests validate all portable output before opening
files. Rejection preserves existing legacy files too; it is not an atomic-I/O
transaction. C database emission rejects malformed decimal metadata rather than
shortening tokens to their parsed prefix. The host-script regression covers
unset, empty, whitespace-only and explicit GoogleTest link flags; blank values
use defaults. Shell argument tests use fake compiler drivers, while the separate
focused gate compiles/runs native code with real toolchains.
