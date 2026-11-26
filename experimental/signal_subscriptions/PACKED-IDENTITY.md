# Experimental packed signal identity

This extends the issue22 subscription experiment on b35673b. Existing
name-based APIs, decoder and metadata structs are unchanged. New C runtime lives in
`j1939_signal_keys.c/.h`, wrapping the original synchronous callback dispatcher.
It is not a stable public API, ABI, global signal number or BRF format.

## Build and example

Host generation additionally needs protoc and Python protobuf (the existing strict
host-model toolchain). Only the host uses protobuf, compilers, JSON and files.
Select an interpreter with MODEL_PYTHON if the driver interpreter lacks protobuf:

```sh
MODEL_PYTHON=/path/to/protobuf-venv/bin/python \
  python3 experimental/signal_subscriptions/run_tests.py --build-dir /tmp/keys-clang
MODEL_PYTHON=/path/to/protobuf-venv/bin/python CC=gcc CXX=g++ \
  python3 experimental/signal_subscriptions/run_tests.py --build-dir /tmp/keys-gcc
MODEL_PYTHON=/path/to/protobuf-venv/bin/python \
  python3 experimental/signal_subscriptions/measure_keys.py \
  --compiler /tmp/keys-clang/dbc_compiler --build-dir /tmp/keys-size
```

run_tests.py builds this checkout's compiler, emits the strict model plus C database,
generates a key catalog, runs generator contracts and sanitized C tests, and checks
the dashboard JSON against its generated manifest. New tests and synthetic catalog
have no private capture/DBC dependency. Target links only generated C tables,
original decoder and the two experimental C modules. No allocator, runtime DBC
parsing, protobuf library, network, background thread or RTOS is required there.

Generation steps are explicit: compiler -model_path; compiler -c_path -c-metadata;
then generate_keys.py --model model.textproto --database subscriptions.c
--metadata-symbol subscriptions_metadata --out-prefix signal_keys --bits 8,8,16.
The host bridge catalog_probe.c compiles/runs trusted generated C and cross-checks
the model against actual C interpretation fields; it is not a second DBC parser.
Do not feed this build tool arbitrary untrusted C source. Host subprocesses do
allocate; the target runtime does not. Validation precedes writing all generated
outputs; an OS failure during later multi-file writes still is not atomic.

```c
static const uint8_t expected_catalog[32] = SUB_GENERATED_CATALOG_SHA256;
j1939_key_context_t context;
j1939_sub_slot_t base_slots[100];
j1939_key_slot_t filters[100];
j1939_key_layout_t layout = { 2, 10, 20 };
/* Check each result; use layout_make for externally supplied integer widths. */
j1939_keys_init(&context, &generated_keys_catalog, expected_catalog, layout,
    J1939_KEY_SOURCE_ADDRESS, 0, NULL, 0,
    base_slots, filters, 100, on_update, application);
uint32_t key, token;
j1939_key_pack(layout, 1, 11, SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED, &key);
j1939_keys_subscribe_key(&context, &context.identity, key, widget_tag, &token);
/* Or wildcard one/both source fields, independently of all valid field values: */
j1939_key_filter_t any = { 0, 0, 1, 1 };
j1939_keys_subscribe(&context, &context.identity,
    SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD, any, load_tag, &token);
j1939_keys_process(&context, incoming_bus_domain, &classic_frame, NULL);
```

The updated dashboard.c uses four generated IDs/packed exact keys and one callback.
The callback stores compact observations; the consumer looks up descriptors by the
unique Signal ID. The same four readings remain1000 rpm,50 percent,2000 rpm and95
kPa. One JSON metadata header establishes the generation/binding; subsequent
observations carry packed keys and data, without repeated binding descriptors.
Python verifies the metadata header before unpacking and associates IDs with the
JSON manifest's names/units. Name selection remains in the original API.

## Bits and scope

A key is domain in the high bits, source in the middle, signal ID in the low bits:
`(domain << (source_bits + signal_bits)) | (source << signal_bits) | id`.
All three widths must be positive and sum to32. C layout_make accepts uint32 input
widths and validates before narrowing; CLI --bits accepts ordinary integers. Each
packing value is uint32 and checked against its mask before a shift. Invalid
layouts/overflow leave outputs unchanged. Tests cover2/10/20,8/8/16 and extreme
1/1/30 and30/1/1 layouts. No shift by32, signed shift or silent masking occurs.
Zero and maximum values are ordinary values, including key0 and UINT32_MAX. Signal
ID0 is valid (the100-signal exercise actually subscribes/emits key0). Wildcards use
separate booleans, never a reserved packed value. Unused wildcard fields must be0;
flags must be0/1. Exact domain/source filters are validated independently.

A catalog ID identifies ONE ORIGINAL definition, including aliases and source-
specific variants, not a PGN, SPN or canonical layout. Assignment is dense zero-
based ordering of (logical source basename, original DBC ID, message name, signal
name), independent of protobuf repeated-record/input-file processing order. SPN
absence, explicit zero and repeats do not affect identity assignment. SPNs remain
in metadata/manifest; keys_by_spn returns every matching ID without allocating.

Inserting/removing/renaming a definition or changing its source/DBC ID can renumber
later IDs. Interpretation-only edits may keep IDs but change catalog identity.
Blank lines/source provenance changes can change identity without renumbering.
Nothing is stable across arbitrary regeneration. Readable SUB_ID_<SOURCE>_<MESSAGE>
_<SIGNAL> macros are ASCII sanitized/uppercased; collisions and identifiers longer
than63 characters reject, as do duplicates and exhausted signal bits. Shorten source
or definition names instead of accepting ambiguous symbols. There is no hash-based
ID collision lottery. Use --catalog-name old_keys/new_keys to give roots, guards, ID macros and header
stamps distinct C namespaces when linking multiple catalogs. This C namespace is
not part of semantic catalog identity or numeric ID assignment.

## Actual address versus logical ECU

SOURCE_ADDRESS takes the actual8-bit SA from the frame, even with a10-bit source
field. Source widths below8 reject in this mode; bits above8 do not extend CAN SA.
Subscriptions/decode reject source values>255. Address moves change the key source
field. Domains are caller-supplied bus/zone IDs, not inferred from the CAN ID.

LOGICAL_ECU requires a caller-owned immutable table of (domain, address0..255,
logical ECU ID). Entries must be sorted by (domain,address) with unique pairs;
duplicates return AMBIGUOUS and missing entries return MAPPING_MISSING. Address
values are uint32 at the C API boundary specifically so300 is rejected instead of
truncating. Logical IDs must fit source_bits;700 or1023 in a10-bit field are ECU
identifiers, not10-bit source addresses. Every incoming frame in logical mode
requires a mapping before delivery, even an unknown PGN. Logical exact/wildcard
selectors must have at least one known and routable mapping.

Multiple addresses may deliberately map to one logical ECU; the table's pair
uniqueness still permits that. A nonzero application-owned source_namespace binds
the meaning/revision of logical IDs. It is zero in raw-address mode. Replacing a
mapping requires a new context initialization after callbacks finish; mappings
must not be mutated live. A generic definition can follow an ECU from SA11 to12
with the same packed logical key. Its mapping fingerprint changes, so exchanging
old keys under the new binding requires explicit migration/retaining the old
binding. Reusing a logical number for a different ECU requires namespace revision.

No J1939 NAME/address-claim tracking is implemented. Source-specific DBC rows remain
SA-specific: a logical mapping alone cannot move such a layout to another SA. If
that address routes to another definition, subscription/decode rejects or delivery
stays on the selected row; it never steals a generic/override interpretation.
PGN/source dispatch and mux activity still use the original decoder. Generic
subscriptions can opt into other addresses but cannot shadow an exact-SA override.
PDU1 DA and priority do not select rows, as before. Domain/source wildcards apply
after canonical routing and mux activity. This wrapper supports J1939 extended
frames; ordinary11-bit frames still work through the unchanged original API but
have no invented SA in this key policy.

## Catalog and binding identity

Initialization requires the generated header fingerprint, which is compared with
the linked catalog before accepting IDs. This catches stale header constants
linked to newer catalog data. Use SUB_GENERATED_CATALOG_SHA256 from that header,
not a runtime copy of the catalog field that would bypass the declaration check.
The consumer keeps one32-byte expected stamp (normally const ROM); it is not
duplicated per signal. Recompile native structs and generated data together.

A naked uint32 is never sufficient for persisted/exchanged interpretation. Supply
a complete j1939_key_identity descriptor to subscribe_key/decode/subscribe. Context
identity compares before unpacking/table lookup. Identity includes:

- catalog SHA256 (32 bytes);
- layout/version1, all three widths and source mode;
- logical source_namespace and mapping FNV1a64 (canonical sorted mapping triples).

The catalog SHA256 covers canonical strict model-v1 contents (all original message,
signal/source properties, exact decimals, units, enums, mux, SPN/default presence,
provenance, nodes and transmitter/receivers), assignment rule/generator version,
actual metadata fingerprint and the ordered ID-to-definition index table. Model
record order is normalized for deterministic generation; original provenance and
semantic list contents remain covered. Output path and native pointers/padding are
excluded; packed layout is checked separately, not folded into catalog SHA.
The manifest layout is a generation default, not an intrinsic catalog constraint;
clients must verify the separately negotiated binding layout. SHA does not hash
raw DBC files directly: their content is represented by parsed model data plus
source byte counts/FNV provenance, not a cryptographic original-file digest.

The host bridge verifies units, four exact numeric strings and binary64 constants,
layout/flags, mux/switch names, enums, SPN/default presence, source fingerprint/byte
count/version and declaration locations against actual generated C. This prevents
a matching name list from disguising a different interpretation. Runtime init also
checks metadata fingerprint and ID-entry fingerprint, so accidentally editing a
constant or reordering IDs rejects despite an unchanged message/record number.

Metadata FNV covers version, full shared string bytes, decode rows, signals and
numeric binary64 bits, enum and fast lookup indexes, source/original-definition
tables and their counts/offsets. Integers use8 little-endian bytes for hashing;
no padding/pointers participate. It assumes the binary64 representation used by
these tested compilers. Mapping FNV covers sorted (domain,address,logical ECU)
triples in the same integer encoding; empty mapping has the defined FNV offset
basis. Binding identity is stored once in the immutable queue metadata blob.
There is no descriptor copy in a keyed update or queued record: the Signal ID
selects the descriptor from the bound metadata. Queue generation guards prevent
old record copies being interpreted under replacement metadata.

These are consistency checks, NOT authentication, and do not prove which ECU sent
a CAN frame. FNV is noncryptographic and collisions are possible. A party able to
rewrite data and accompanying fingerprints can bypass checks. Generated catalogs
and metadata are trusted native tables, not untrusted binary parsers. No standalone
editable hash is advertised as authentication. A real gateway needs its own
trusted configuration/transport boundary. The prototype does not serialize native
identity structs as a wire ABI. JSON uses named fields, unsigned numeric keys and
strings for64-bit hashes/namespaces/raw/timestamps. No BRF changes.

For a dashboard: negotiate/verify the complete descriptor, find the matching
signal_keys.json by catalog_sha256, check widths/source policy/namespace/map hash,
then unpack signal ID and index manifest.signals[id]. Check actual source
applicability too (the C decode helper does). JS can use BigInt(key) for shifts to
avoid signed32-bit bitwise results; every uint32 key itself is exactly representable
as a JS Number. Raw64-bit integers remain strings/BigInt. Reject a descriptor or
out-of-range ID before presenting labels/engineering metadata.

## Delivery and measurements

The wrapper calls the existing synchronous dispatcher and filters its updates; it
adds no background thread, clock, heap or DBC parse. The optional caller-owned
queue module is described below. Exact keys and explicit
wildcards share one callback. Scale/raw/unit/time/error/unsubscribe behavior remains
as specified in README.md, including whole-DLC rejection and unknown quality. The
base context inside a key context is internal; do not call its process/mutation
APIs directly to bypass key routing. All config/tables must outlive the context;
synchronous updates borrow generated metadata. Compact queue records contain
only packed keys and observation/delivery data; metadata is held once per queue.
Key API calls during callback return BUSY; init/re-init during callback is forbidden.
Different contexts need caller synchronization just as before. Registration tokens
are not packed keys; token0 is invalid, key0 is valid.

The target wrapper adds a20-byte filter slot per registration plus a larger
context, separate from legacy slots. One metadata/source pointer/config is shared;
layouts/numeric/enum strings are not copied into a second embedded catalog. The
ID projection is8 bytes per definition plus a native root. The full manifest is
host/web data, not embedded ROM. Callback/preflight/lookup costs increase: filtering
currently happens after the underlying decode, and init validates uniqueness in
O(number of IDs squared). This is a correctness experiment, not a throughput claim.
A future combined dispatcher could eliminate duplicate slot state and filter first.
A connection-level immutable binding could reduce per-update copying.

## Queue metadata ownership and double buffering

j1939_key_record_t is40 bytes on both tested ABIs: packed key, exact raw, actual
CAN ID, timestamp/presence/clock, quality, subscriber token and tag. There are no
units, scale/offset, descriptor pointers or56-byte identities per entry. The unique
Signal ID resolves through j1939_key_queue_descriptor into the metadata blob;
engineering conversion and units are obtained on consumption. Recomputing physical
values in the consumer currently repeats work done by the original dispatcher.

One immutable64-byte queue metadata blob contains the binding snapshot and producer
reference. Queue controls hold one metadata pointer and generation. Metadata blobs,
catalogs and mappings must remain valid/immutable while bound. Do not overwrite a
live blob with metadata_init. Replace through one of these explicit policies:

- queue_replace is destructive: serialize producer/consumer, flush, swap blob,
  increment generation and invalidate old popped-record generation tokens, even
  for identical fingerprints. In-place producer reinit that changes its binding
  is detected as stale by the single-queue path before descriptor lookup.
- key_pair_switch is non-destructive: two separate caller-owned queue/metadata/
  context banks coexist. Switch active producer at a frame boundary; drain old
  records against the OLD manifest before newer records. A third update returns
  BUSY while an old bank still has data. Ask pair_reusable before preparing that
  bank again. No handoff flush occurs. Generation numbers increase across banks.

Consumer callbacks finish decoding/copying/serialization before returning; the
pair keeps the bank leased through that callback and rejects reentrant switching.
It does not support asynchronous descriptor borrows beyond callback return. All
pair operations require external serialization if threads/interrupts are involved;
publication is not a lock-free atomic C implementation. Bounded drop-newest queue
policy still applies when a consumer cannot keep up. Two buffers avoid transition
loss within capacity, not unbounded losslessness. A third metadata update must wait
or use caller-provided extra storage. CAN sources may not support backpressure.

The tests compile two real catalogs with the SAME Signal ID/key but scale/unit
changes, observe7 old-unit then14 new-unit under their respective manifests, and
verify third-update backpressure, no transition invalidation/drop, FIFO order,
generation exhaustion and observable overflow. The existing descriptor resolver
is context-bound, so this prototype retains two producer contexts while draining;
that coupling is not fundamentally required. A future metadata-only resolver could
share one active dispatcher without retaining old subscription tables. Never
reinitialize the old context before its consumer callback has finished here.

A web gateway can send one metadata header per generation/session, then its compact
observations. An ordered consumer retains each manifest until the associated data
is consumed. Non-FIFO/multiplexed transports need an explicit channel/batch/epoch
association; a bare32-bit key cannot identify arbitrary catalog revisions.

Measured same synthetic100-signal/30-PGN catalog (Apple Clang17 -Os):

| Item | x86-64 previous -> packed | Cortex-M4 previous -> packed |
| --- | ---: | ---: |
| Caller context +100 subscriptions, no queue |3240 ->5352 B|2824 ->4920 B|
| Synchronous callback update, not queue format |104 ->120 B|96 ->112 B|
| Eight compact queue records |320 B|320 B|
| Queue controls +one metadata blob |104 B|96 B|
| Complete optional eight-record queue |424 B|416 B|
| Key runtime .text, excluding queue/decoder/support |3293 ->9766 B|2394 ->8674 B|
| Optional queue +double-buffer controller .text |2306 B|1736 B|
| Catalog const data (metadata + ID projection) |12152 ->13024 B|12065 ->12929 B|

Compared with the superseded descriptor-per-update draft (176/168-byte updates),
eight raw queue entries are320 B instead of1408/1344 B. This is a different compact
record representation, not merely removing56 B. Binding is once per metadata blob.
The ID projection adds872/864 B; one required header stamp costs a further32 B of
application const storage. Queue and key runtime have no writable globals. Extra
unwind sections are reported separately in measurement JSON, not .text totals.

Two banks each with100 subscriptions and8 records use11600 B host /10704 B ARM
including contexts, slot arrays, queue records/controls/blobs and pair controller.
This is exact sizeof arithmetic, not a200-signal throughput benchmark. Dynamic
handoff tests use one signal/two records per bank. Separate catalog/mapping versions
may require additional const storage; immutable tables can be shared when unchanged.

Reproduce measurements with measure_keys.py. Its selected build directory contains
packed-results.json, packed-commands.json and nested measurement outputs. Clang -Os measures both x86-64 and Cortex-M4 objects; GCC
also runs host sanitizer tests, not ARM code. Object sections exclude final link
padding, decoder/support code, stack, caller callbacks/stdio, registration lists,
transport buffers and actual firmware placement. Optional mapping rows are12 bytes
each and may be const ROM. No target execution, cycle benchmark or RTOS validation
is claimed. Previous full-suite/coverage results are not relabeled as new runs;
the seven legacy C++ UBSan diagnostics remain open.

## Review conclusion

The existing C metadata and strict model are sufficient for this prototype. No
fields were added to their public structs and no Quarry dependency was introduced.
The C projection retains generated identity separately from interpretation data;
JSON metadata enables language-specific generators to share IDs and semantics.
C++, Python and Go can implement the same bounded unsigned packing, descriptor
comparison and exact integer model using shared vectors/manifest. Whether runtimes
are native or C bindings remains undecided; no four-language implementation here.

Before a public API, review dense-ID migration, multi-catalog symbol namespace,
logical namespace ownership, address-claim/DA policy, hash/version portability,
canonical external descriptor serialization, startup/queue overhead and the lifecycle of metadata-only
consumers and session/epoch transport binding. Keep experimental status and review
these tradeoffs before considering main. No new build option is needed to hide
unmeasured performance costs.

## Independent-review fixes: lifecycle contract

Queue initialization starts a **fresh incarnation** at generation 1. Before
initializing or reusing object storage, the caller must retire every old record
handle and borrowed pointer, stop callbacks, and externally serialize all users.
Tokens are scoped to one queue object and incarnation, not globally unique.
Initialization cannot recognize an old token after storage reuse. Use queue_replace
for a live destructive reset: it flushes records and advances the existing epoch,
rejecting UINT64_MAX without changing the queue. There is no implicit shutdown:
quiesce producers, drain or explicitly abandon records, retire borrows, then release
objects. Fresh init is not a safe shortcut for resetting a live queue or bank.

Pair initialization adopts the maximum existing bank generation and rejects an
exhausted bank transactionally. Switching advances above both banks and the pair,
including generations advanced while preparing a reusable inactive bank. Never
queue_init an inactive bank of a live pair; prepare its producer/blob while safe
and queue_replace its queue to preserve the generation. Each bank's objects retain
fixed addresses and disjoint storage until the pair is retired. Metadata blobs,
producer configuration, catalog and maps remain immutable/alive until all dependent
records and borrowed references are retired. Failed init leaves its output object
unchanged; it does not release or transfer ownership.

Enqueue is now **callback-only**: push accepts only the exact update pointer
currently being delivered by that queue's bound producer. A saved/copied update,
including one from the same producer storage before rebinding, returns MISMATCH
without changing records, counters or control state. Callback must return normally;
longjmp, mutating the producer and retaining update pointers are unsupported.
A saved value can be used by an application's separately designed observation
pipeline, but cannot be replayed through this enqueue API. The implementation adds
one current-update pointer per context, not per queue entry. This checks lifecycle,
not authenticity or protection against callers modifying internal structs.

All process, drain, preparation, replace and switch operations require caller
serialization. Busy flags prevent synchronous reentrancy only; they are not locks
or atomics. Consumers finish interpretation/copying before returning. Drain pops
before invoking its void callback: sinks must accept/copy synchronously; there is
no acknowledgement/retry, durable delivery or asynchronous descriptor lease.

The unchanged 40-byte record (tested native and Cortex-M4 ABIs, alignment 8) has
raw uint64 at offset 0, timestamp ns uint64 at 8, packed key uint32 at 16, actual
CAN ID at 20, clock ID at 24, subscription token at 28, tag at 32, timestamp-present
byte at 36, quality byte at 37 and two padding bytes. Timestamp zero is valid.
CAN priority/destination and actual address in logical mode cannot generally be
recovered from the key; subscriber token/tag are registration data. Units, scales,
signedness and enums already live in metadata. Independent scratch alternatives
measured 32-byte observation-stream and 24-byte bank-clock records but change
semantics; removing CAN ID alone still measured 40 bytes. No layout change here.

Generated header guards use J1939_GENERATED_PACKED_CATALOG_<NAMESPACE>_H, separate
from known runtime guards. Catalog namespaces j1939_db,
experimental_j1939_signal_keys and j1939_key_queue compile in permanent tests,
alone and together. This does not establish a stable public symbol namespace.

The earlier footprint table is historical (before the callback-lease pointer).
Reproduce current measurements with measure_keys.py; the earlier table is historical.

## Latest accepted observations

See [LATEST-VALUES.md](LATEST-VALUES.md) for the separate caller-owned latest store,
validated generic/typed lookup, reset/binding rules and upper-layer age policy.
Event FIFO metadata/bank ownership rules above are unchanged.

## Canonical catalog namespaces (review fix)

`--catalog-name` now requires canonical lowercase snake case:
`[a-z][a-z0-9]*(?:_[a-z0-9]+)*`. Single internal underscores are allowed;
uppercase, punctuation, leading/trailing/repeated underscores and leading digits
reject before output. Nothing is silently lowercased or sanitized. `foo` is valid;
`Foo` is rejected with a canonical-lowercase diagnostic.

The exact `id` segment is reserved in any position, and `sub`/`sub_generated` are
reserved aliases for the default catalog's legacy macros. This is necessary beyond
case folding: allowing `foo_id_bar` would let its ID prefix nest under `foo`'s
`FOO_ID_` prefix, and accepting `sub` or `sub_generated` would overlap the default
SUB_ID_ or SUB_GENERATED_* names. The unique `_ID_` delimiter cannot appear in an
accepted uppercase catalog namespace. Definition-name sanitization collisions
continue to reject within a catalog rather than arbitrarily changing a signal ID.

For accepted catalog names, existing roots, guards, ID constants, layout and stamp
macros are byte-for-byte unchanged: `old_keys`, `new_keys`, `generated_keys`, `foo`
and `foo_bar` keep their existing meanings. The default SUB_* aliases remain for
compatibility. Previously accepted mixed-case/noncanonical/reserved names now
require an explicitly chosen canonical replacement and regeneration of C consumers.
Numeric signal IDs and catalog fingerprints do not depend on the C namespace;
namespace migration alone does not change their semantic identity. Do not merely
reuse old generated headers under an unreviewed renamed C namespace.

Every generated public identifier (root, guard, stamp, layout and signal macros)
is checked for collisions and the existing 63-character limit. The supplied
metadata symbol cannot equal any generated public identifier. Validation failure
leaves existing .h/.c/.json outputs unchanged and creates no new outputs. OS failures
during the subsequent multi-file writes remain non-atomic, as documented before.
Distinct accepted namespaces coexist; one namespace must not be reused for two
catalogs in the same translation unit/program. Output basenames must still be
simple C identifiers without a suffix; choose distinct filesystem paths separately.
