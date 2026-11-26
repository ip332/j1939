# DBC model v1: protobuf Text Format

`dbc_compiler -in selected.dbc -model_path selected.textproto` emits
`j1939.dbc.model.v1.Model` from [dbc_model_v1.proto](../schema/dbc_model_v1.proto).
This is a host interface: a separate integration tool reads definitions and chooses
record mappings. Embedded targets compile J1939 C codecs; they need neither DBC
parsing nor protobuf. J1939 does not generate BRD or link Quarry/protobuf runtimes.

The CLI output mode and a versioned intermediate model serve different purposes:
the mode is the delivery mechanism, and the schema is the consumer contract. This
keeps Quarry's naming, schema evolution, quality and record semantics outside the
DBC compiler. A dedicated BRD backend would couple those policies to J1939 releases.

## Usage and consumer

```sh
dbc_compiler -in tests/dbc/model_v1/single_frame.dbc \
  -in tests/dbc/model_v1/multiplex.dbc -model_path model.textproto
mkdir -p /tmp/dbc-model-python
protoc -I schema --python_out=/tmp/dbc-model-python schema/dbc_model_v1.proto
PYTHONPATH=/tmp/dbc-model-python python3 examples/read_dbc_model.py \
  model.textproto --dbc-id 100
```

The Python consumer requires the `protobuf` package compatible with the installed
`protoc`. Generate bindings in the consumer's build directory; generated bindings
are not checked into J1939. Other languages can generate bindings from the same
schema and use their protobuf Text Format parser. Parse with unknown fields rejected,
check required-field initialization, and require `format == "j1939.dbc-model"` and
`version == 1`. See the official [Text Format specification](https://protobuf.dev/reference/protobuf/textformat-spec/).
The example selects by original DBC ID and prints a message; it is not a complete
semantic validator, adapter generator, or CAN decoder.

`-model_path` is exclusive: combining it with another output mode, `-name`, or
`-no-strings` fails before opening outputs. All sources are validated before the
output is opened. Validation failure preserves an existing output file. I/O errors
return a failure status; output replacement is not crash-atomic. Do not use the
same path for an input and output. Parse/validation errors return exit code 4;
option/output errors return 2. No model is emitted on parse failure.

The C++ interface is `setModelExportMode()` before any `parseFile()` calls, followed
by `generateModelOutput(out, error)` before `removeDuplicates()`. A failed strict
parse poisons that parser's export session; create a new parser to retry. Legacy
parsing and normalized PGN maps cannot be passed off as strict model exports.
The strict path is part of `DbcParser`, uses the shared `Message`, `Signal` and
`VAL_` storage, and validates before constructing the existing model. There is no
second parser or CAN codec in the consumer.

## Supported input subset

Every input message must satisfy the subset; selection happens in the consumer.
There is no option that silently drops unsupported messages. Start with small,
explicitly curated single-frame fixtures rather than a full vendor DBC.

* Printable ASCII source text, LF or CRLF, tabs/spaces between tokens; ASCII DBC
  identifiers `[A-Za-z_][A-Za-z0-9_]*`. Quoted units, enum labels and VERSION text
  preserve spaces and tabs. DBC backslash escapes, embedded quotes, non-ASCII and
  multiline strings are rejected. Spacing inside bit-layout/number tuples is not
  accepted; see the fixtures for the grammar. No C-style numeric suffixes.
* Optional VERSION, NS_ capability-name list, empty BS_:, and BU_: node-name list.
  VERSION, nodes and namespace symbols are retained. A listed NS_ capability does
  not imply its declarations are supported. An empty BS_: has no bitrate to retain;
  populated bitrate headers are rejected.
* BO_ IDs: standard 0..0x7ff, or explicit DBC extended flag 0x80000000 plus a 29-bit
  ID. Flagless extended IDs and other flag bits fail. The J1939 reserved ID bit
  25 is rejected. DLC is 1..8, with at least one signal. Names and original IDs
  must be unique across all input files, even for otherwise identical rows.
* SG_ integer signals, widths 1..64, Intel or Motorola, signed or unsigned. Mixed
  byte order within a message is supported. All touched bits must fit the DLC;
  simultaneously active signals must not overlap. Physical scale must be nonzero
  as a finite host double; scale/offset/range tokens must parse as finite doubles,
  with minimum <= maximum under that host representation. Decimal source tokens
  remain authoritative in the export. Transmitter and receiver names are retained;
  node linkage is not validated against BU_: declarations.
* One unsigned non-nested `M` switch per multiplexed message; `m0`..`m65535` branches
  reference its raw value. Branch values must fit the switch. Overlap is permitted
  only between branches with different switch values. Unconditional signals cannot
  overlap branches or the switch. The switch's scale/offset do not affect selection.
* VAL_ entries are unsigned raw wire patterns that fit the signal width, including
  uint64 maximum, with quoted labels. One VAL_ declaration per signal; duplicate
  values are rejected, even if labels match. Negative/signed enum literals are not
  converted to wire patterns implicitly.
* Explicit `BA_ "SPN" BO_` / `SG_` uint32 assignments and boolean
  `BA_ "J1939Default" BO_` assignments. References must follow their message/signal
  declarations and stay in the same source file. Repeated assignments fail.
  These explicit attributes are accepted without BA_DEF_ declarations; attribute
  definitions/defaults/inheritance are outside v1 and rejected.

Everything else is rejected, including comments (`CM_`), custom attributes,
BA_DEF_/BA_DEF_DEF_, frame-format overrides, float-wire SIG_VALTYPE_, extended mux
ranges SG_MUL_VAL_, nested mNM, signal types/templates/groups, value tables,
environment variables, relations, CAN FD and transport payloads. The strict mode
never infers missing wire semantics from skipped records. Legacy output modes
remain more permissive and are not a substitute for this contract.

## Model semantics

The `.proto` is the field/type contract. Required fields are always written,
including false booleans, explicit empty unit strings and zero-valued enums. An
optional field is omitted only when its information is absent or not applicable;
consumers must use presence, not its protobuf default value.

| Field group | Meaning |
| --- | --- |
| `format`, `version` | Fixed format identifier and schema version 1 |
| `sources` | Unique logical basenames, exact-byte length/checksum, optional VERSION and header metadata |
| `messages.dbc_id` | Original BO_ ID, including extended flag; no PGN collapse or alias stripping |
| `can_id`, `extended` | Original wire ID and explicit frame type; priority/SA/destination bits remain in ID |
| `pgn`, `source_address` | Derived J1939 interpretation of extended ID only; omitted for standard frames |
| message `spn`, `j1939_default` | Explicit attribute values only; absent differs from 0/false; no generic layout is inferred |
| `source`, `line` | Source name and 1-based BO_/SG_ declaration line, before normalization |
| `start_bit`, `width`, `byte_order`, `is_signed` | Original signal layout; Motorola start is the MSB in DBC sawtooth numbering |
| `scale`, `offset`, `minimum`, `maximum` | Exact decimal strings, retaining spelling, exponent, signs and trailing zeros |
| `unit` | Complete unit text without DBC quotes; `""` is explicit empty text, not inferred units |
| signal `spn` | Optional assignment, including explicitly assigned 0 |
| `multiplex` | Absent for unconditional signals; SWITCH role or BRANCH role with switch name/raw value |
| `enums` | Numeric uint64 raw patterns and exact label text, not physical values or quality classifications |

PDU1 (`PF < 240`) excludes destination PS from PGN; PDU2 includes it. Export does
not say that any one BO_ row is a generic PGN layout. Distinct source/priority rows
remain distinct. A consumer must choose exact message identity and define any
PGN/SA/default policy before matching the generated C database's collapsed rows.

Protobuf integers preserve all 64 bits. Decimal strings avoid binary floating-point
round trips and locale formatting; consumers should use decimal/rational arithmetic
when generating field types or fixed-point code. Host double validation is a bound
on the parser's supported domain, not an assertion of decimal arithmetic precision.
The model does not supply physical-value types, schema IDs, timestamps, NAME lookup,
validity, NA/error/reserved patterns or application-level units conversions.

## Provenance, determinism and evolution

Source identity is the input basename, including `.dbc`; duplicate basenames fail
rather than exposing checkout paths. Basenames must match `[A-Za-z0-9_.-]+.dbc`.
`byte_length` and `fnv1a64` describe exact input bytes, including whitespace and line
endings. The checksum is FNV-1a 64 with offset 14695981039346656037 and multiplier
1099511628211, modulo 2^64, rendered as 16 lowercase hex digits. This is a
noncryptographic provenance checksum, **not** a trusted content identity, authenticity
check or schema ID. Keep original source files in the build's provenance manifest;
use a cryptographic digest there when required. Declaration lines locate BO_/SG_
records, not every subsequent attribute/VAL_ statement. All metadata in a source is
covered by its checksum. No time, absolute source path or host/compiler version is
inserted into the model.

For identical logical filenames and input bytes, output is byte-identical across
input order and checkout paths: sources by basename, messages by original unsigned
DBC ID (standard before extended), signals by start bit then mux value then name,
enums by raw value, nodes and namespace symbols lexically. Receiver order follows
the DBC list. Fields are written in schema order with fixed ASCII escaping, two-space
indentation and LF. This is the exporter v1 convention; protobuf Text Format writers
in general are not promised to emit the same bytes.

The v1 schema/package is frozen. A field rename, addition, deletion or semantic
change requires a new export version/package; strict text readers reject unknown
field names. Consumers must not silently ignore unknown fields or coerce unknown
versions. Future CLI format selection can coexist with v1; current `-model_path`
emits v1 only. Byte-for-byte golden fixtures cover the writer convention.

Diagnostics identify source path/line and the rejected declaration, or message and
signal for semantic errors. A semantic error may be reported after the source has
been read, with the offending declaration location inside the message. No assertions
or first-wins duplicate resolution are used in the strict input path. Empty models,
missing files, duplicate logical names and out-of-range values fail explicitly.

## Validation and next consumer work

Run `bash scripts/dbc_model_tests.sh` with a C++14 compiler, protoc and Python
protobuf available. On this macOS host, legacy host code needs:

```sh
J1939_MODEL_CXXFLAGS='-Wno-vla-cxx-extension -Wno-deprecated-declarations' \
  bash scripts/dbc_model_tests.sh
```

The script builds the exporter, generates Python bindings, and tests parsing with
protobuf itself, selection without reopening DBC, exact values/presence, unit text,
provenance checksum/lines, mux references, uint64 enum precision, deterministic golden
output, relocation/input order, version/unknown-field rejection and preservation of
existing outputs on failure. `DbcModelTest` in the existing host harness additionally
covers malformed/unsupported records, duplicates, missing references, overlapping
layouts, invalid muxes, normalization misuse and fail-without-output behavior.
Existing C codec/transport tests remain independent.

Quarry can vendor/pin this `.proto` contract or acquire it as a build input, generate
host bindings, read strict Text Format, select original messages, and apply its own
record mapping. It still needs naming/collision rules, stable schema identities and
evolution, raw versus physical precision, metadata/timestamp/SA policy, units,
quality patterns and mux-to-record rules before generating BRD and C adapters.
Adapters should call the J1939-owned generated accessors/C decoder; neither BRD nor
BRF needs to duplicate CAN layouts. Multi-frame transport is outside this version.
