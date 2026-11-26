# Experimental individual-signal subscriptions (issue 22)

The optional [packed-identity extension](PACKED-IDENTITY.md) now adds generated
IDs and configurable keys through a separate wrapper. The name-based API below
is unchanged. The test driver additionally needs host protoc/Python protobuf; set
MODEL_PYTHON to a protobuf-enabled interpreter if necessary. Target dependencies
remain unchanged.

Experimental C vertical slice based on `experimental/c-metadata` commit
`b35673b71dab177caee71e11248f375f9ae07c85`. This is a prototype for review,
not an installed SDK, stable API/ABI, persistent identifier scheme or merge proposal.
The existing compiler, database, decoder, BRF and Quarry are unchanged.

## Run and use

From the repository root, with a C11 compiler, C++14 host compiler and Python 3.8+:

```sh
python3 experimental/signal_subscriptions/run_tests.py --build-dir /tmp/subs-clang
CC=gcc CXX=g++ python3 experimental/signal_subscriptions/run_tests.py --build-dir /tmp/subs-gcc
```

The driver compiles this checkout's DBC compiler, generates the opt-in C database
from `tests/dbc/subscriptions_synthetic.dbc`, and compiles/runs the C tests and
`dashboard.c` under fail-fast ASan/UBSan. The final JSON output is independently
parsed/checked by the Python driver. All build artifacts are outside the source
tree. No private capture, proprietary DBC, protobuf runtime, Quarry, CAN driver,
network, RTOS, or test-framework library is required. Cross-platform compiler
selection is through CC/CXX; HOST_CXXFLAGS allows host compiler warning exceptions.
Only the host compiler parses the DBC; the target links generated C, the existing
`j1939_db.c` and this directory's `j1939_subscriptions.c`.

```c
j1939_sub_context_t context;
j1939_sub_slot_t slots[100];
/* on_update(void *application, const j1939_sub_update_t *update) */
j1939_sub_init(&context, &subscriptions_metadata, slots, 100, on_update, application);
/* Check every return code; abbreviated here. */
j1939_sub_ref_t speed;
uint32_t token;
j1939_sub_select(&context, "subscriptions_synthetic.dbc", "EngineA", "Speed", &speed);
j1939_subscribe(&context, speed, J1939_SUB_DECLARED_SOURCE, 42, &token);
/* Repeat selection/registration in a data-driven loop for other signals. */
j1939_sub_frame_t frame = {0};
frame.can_id = received_id;  /* raw identifier, no SocketCAN flags */
frame.extended = 1;
frame.data = received_bytes;
frame.size = received_length;
frame.timestamp = timestamp_or_null;
j1939_sub_process(&context, &frame, NULL);
j1939_unsubscribe(&context, token);
```

`dashboard.c` registers Speed and Load from EngineA, source-specific Speed from
EngineB, and Motorola Pressure from another PGN using one callback and a caller
tag per registration. It emits four JSON observations, including 1000 and 2000 rpm,
50 percent, and 95 kPa. It serializes wide integers/timestamps as strings so a
browser can use BigInt. Nonfinite engineering values serialize as null. stdio is
host example code, not a runtime requirement. No WebSocket server/browser code is
inside the C runtime. A real gateway also needs explicit configuration identity,
JSON transport, access control and any necessary timestamp conversion.

## Selection, applicability and identity

Selection is an exact case-sensitive lookup of logical DBC source basename,
original message name and original signal name in metadata's original-definition
tables. Alias names survive even though compact name indexes expose representative
names. Selection stores metadata pointer plus definition indices; it does not copy
layouts, enums, numeric constants or units. Startup lookup is linear. A future
generator can emit constants referring to these entries, but indices can change
when configuration/order/definitions change. The original name-based API provides no generated ID contract; the separate
packed wrapper adds experimental catalog-scoped identifiers. Treat references as scoped to this immutable database object for its
lifetime; a reference for a different metadata object is rejected even if that
object has identical contents. Tokens and caller tags are context-local, not CAN
signal identities. A token is never reused within an initialized context: after
UINT32_MAX successful registrations further registrations fail with ID_LIMIT.
Reinitialization resets token numbering: old tokens must not be used because
they can collide with new registrations. Tokens are not safe across initialization
lifetimes or between contexts. Selection references instead belong to the metadata
object and can be reused across contexts while that same immutable object lives.

On each frame, existing `j1939_db_find` resolves standard CAN ID or PGN with exact
SA overrides before generic rows. A registration matches only its canonical decode
row; generic subscriptions cannot override a source-specific interpretation.
Default DECLARED_SOURCE additionally requires the original definition's SA. An
alias registration therefore does not accidentally receive every source that
happens to share its row. ANY_ROUTED_SOURCE explicitly accepts any SA routed to
that row, including sources not declared in the DBC. This is a caller policy,
not proof that all those sources use the interpretation. Single-layout generic
row inference remains the compiler's historical policy, not a universal DBC claim.

Priority and PDU1 destination do not select rows in the existing decoder: a PGN's
PDU1 destination is normalized out. This prototype similarly filters SA, not DA;
updates retain the actual CAN ID for applications needing destination context.
Tests exercise different destinations for one PDU1 PGN. Destination filters,
dynamic J1939 NAME/address association and multiple interpretations per source
need explicit future API policy. Static source declarations alone cannot supply it.

Metadata FNV is a provenance checksum, not a configuration key or authentication.
Pointer equality/context association catches local accidental bundle mixups only;
a caller capable of rewriting data/tables can bypass it. No cryptographic identity
or authentication guarantee is added. Existing Quarry mapping/envelope rules
still apply to a separate Quarry adapter; no record ID or subscription token proves
an interpretation.

## Values, errors and time

- `raw` is the exact uint64 wire pattern, zero-extended to its width; signedness
  and width remain in `signal`. Signed signals additionally expose exact
  `signed_raw`, including INT64_MIN. Unsigned signed_raw is not meaningful.
- `engineering` uses existing `j1939_signal_phys`: signed interpretation, numeric
  scale then offset. The prototype requires binary64 double at compile time.
  Integer values above 2^53, extreme signed limits and decimal factors can round;
  UINT64_MAX becomes 2^64 as double. Exact raw integers and original decimal text
  in `interpretation` remain available. `engineering_finite` reports arithmetic
  representability only; overflow remains a delivered raw observation with a
  nonfinite engineering result. No exact-decimal runtime parser is added.
- `unit` is an immutable borrowed database string. Explicit empty units return
  non-NULL `""`. Missing/malformed units fail strict generation; missing/invalid
  metadata rejects initialization. Ranges and enum labels do not determine
  quality: quality is always UNKNOWN, even for labels suggesting reserved/error.
  Enums and exact scale/offset/range text remain reachable via the update pointers.
- Only classic CAN integer signals (1..64 bits) and simple mux from the strict
  metadata profile are supported. Inactive branches emit nothing; no invalidation
  event or automatic erasure of a previous observation is implied. Applications
  wanting latest values must own their cache and mux validity policy.
- IDs must be raw 11/29-bit identifiers and extended must be 0 or 1. Bad IDs,
  pointers or a matched frame shorter than the row's whole nominal DLC return
  INVALID without callbacks or changing out_count. FD (>8 bytes) and reserved
  extended-ID bit reject as UNSUPPORTED. Caller provides at least size readable
  bytes; C cannot discover the actual allocation length. Padding up to 8 bytes is
  accepted. This frame type represents classic data frames; the transport adapter
  must reject RTR/error and CAN FD frames before construction. An FD frame of 8 or
  fewer bytes cannot be distinguished from classic CAN from these fields alone.
  Unknown valid IDs return success/count0. A known short frame rejects
  even if no subscription currently matches. No TP/fast-packet reassembly occurs;
  this only processes individual classic frames. Unsupported DBC dialects reject
  upstream; legacy databases without the extension cannot initialize a context.
- Optional timestamps are caller-supplied uint64 nanoseconds plus a caller-defined
  clock-domain ID. NULL means absent; nanoseconds0 is valid. Updates preserve
  presence, clock ID and ticks exactly; no arrival clock, wall epoch, monotonicity
  or ordering is inferred. Absent timestamp fields must be ignored. Adapter code
  must establish units/epoch before converting another source's timestamp.

## Delivery, lifetime and concurrency

One initialized context has one callback and application pointer; multiple
registrations (including duplicate signal selections) carry independent tokens
and tags. Every active observation is delivered; no change filter or rate limit.
Callbacks run synchronously during process in ascending occupied slot order.
After unsubscribe a free slot may be reused, so ordering is not permanently
registration order. Frame calls determine observation order, not timestamps.
Inactive/unknown inputs deliver zero observations. Storage exhaustion returns FULL
without replacing a registration. Unsubscribe removes that token for subsequent
frames; double/stale unsubscribe returns NOT_FOUND. Caller output parameters are
unchanged on errors. No background task, queue or latest-value cache is allocated.

All matching extractions are preflighted before the first callback. The second
pass delivers without a frame-sized temporary array; caller must not mutate
frame, payload, timestamps, context storage or metadata during process. A callback
may copy an update, but the update pointer itself is valid only until callback
return. Metadata, strings and all referenced generated storage must outlive the
context and any saved/queued update copies. No frame payload pointer is retained.
Callback must return normally (no longjmp or C++ exception across C), and must not
reinitialize the context. Select/subscribe/unsubscribe/process on that same context
inside delivery returns BUSY. Defer mutations until process returns. Calls on
separate contexts are independent; no context is internally thread-safe or atomic.
Single-owner use or external serialization is required, including init and callback
user state. The busy flag detects reentrancy, not data races. Do not call from an
ISR unless caller constraints and worst-case callback/decoder cost permit it.

For asynchronous web delivery, the caller can provide a ring of 8 update structs:
callback copies `*update` into the next free slot and returns; an application event
loop drains copies, adds interpretation/configuration identity, serializes JSON
and sends WebSocket messages. Choose a bounded policy explicitly, e.g. drop-newest
and increment a loss counter when full. The void callback does not block or signal
backpressure to this runtime. Cross-thread producer/consumer synchronization is
caller-owned. Copied strings remain borrowed; unload/reconfigure only after draining
or copying the required string text. This ring is a design sketch, not a supplied
or validated concurrent queue implementation.

## Cost and validation

`measure.py --compiler /tmp/subs-clang/dbc_compiler --build-dir /tmp/subs-size`
creates a redistributable synthetic catalog of 100 subscriptions across 30 PGNs,
processes 30 frames and checks 100 raw/physical updates. It uses llvm-size/llvm-nm
and Clang Cortex-M4 cross-compilation; override tool paths if needed. Core code
uses no libc string routines, malloc/free, DBC parser, protobuf or Quarry. Compiler
support routines remain ordinary target link requirements.

Measured Clang -Os, function/data sections (object sections, not final firmware):

| Item | x86-64 macOS | Cortex-M4 Thumb |
| --- | ---: | ---: |
| Context |40 B|24 B|
| Subscription slot |32 B|28 B|
| Context +100 slots |3240 B|2824 B|
| One copied update |104 B|96 B|
| Optional8-update queue contents (no control/lock fields) |832 B|768 B|
| Prototype .text |3293 B|2394 B|
| Prototype extra const / unwind |24 B /192 B|0 B /48 B|
| Compact generated100-signal catalog |7544 B|7486 B|
| Full metadata generated catalog |12152 B|12065 B|

Generated tables/prototype have no writable global storage. Context and slots
are caller-owned RAM. These figures exclude call-stack use, callback/application
state, decoder code, compiler-support libraries, transport storage, queue controls,
link padding, relocations, startup flash/RAM placement and debug/object headers.
ARM prototype references `__aeabi_dcmple`, `__aeabi_memclr8` and existing decoder
functions; the prototype object alone is not a runnable firmware image. The x86
prototype's unwind data and ARM exidx are listed separately. No target execution,
RTOS validation, cycle/latency benchmark or whole-firmware RAM claim is made.

Processing scans capacity twice plus existing message lookup and mux extraction;
only matching subscriptions decode, currently twice each for preflight/delivery.
Duplicate subscriptions also decode separately. Initial context validation and name
selection scan metadata; no runtime CAN observation triggers a DBC parse. Profile
an actual target before adding dispatch indexes/caches or promising 100-signal
throughput. A unique-signal dispatch index could reduce repeated decode work later. The
prototype also repeats the metadata pointer in each slot selection; reconstructing
it from the context could save400 B per100 slots on ARM (800 B on this host).
That optimization is not implemented or included in the reported RAM figures.

## Review recommendation and remaining work

Metadata-v1 is sufficient for this C experiment without adding a second catalog
or changing the foundation. Original-definition aliases, source declarations,
units, exact numeric text and core enums/mux are usable through references.
Before a public subscription API, review explicit source/destination/NAME policy,
interpretation/configuration identity, generated identifier scope, callback/queue
contracts and ABI version/packaging. A direct stable ID lookup and reverse index
could improve startup/runtime cost, but should not duplicate layout/numeric data.
Signed raw interpretation and floating conversion need one written cross-language
contract and shared independent vectors; current C vectors cover Intel/Motorola,
scaling, wide signed limits, mux, sources, timestamps and overflow. Python tests
independently verify dashboard JSON. Consistent C++/Python/Go generation can use
the same strict host model and semantic vectors; native dispatch versus shared C
bindings remains undecided. No four-language implementation or C FFI mandate is
introduced here. Quality, timestamp adapters, latest storage and concurrent queues
need separate policy and tests. Existing adjacent-message legacy parser assertion
and seven legacy C++ UBSan findings remain separate follow-ups; the wider host
suite is not claimed sanitizer-clean.
