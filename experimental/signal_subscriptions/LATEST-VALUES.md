# Experimental latest accepted signal values

This extends the packed-identity prototype on b35673b. It is an experimental C
API, not a stable ABI, wire format or generated registry. The three independent
review fixes remain in place: monotonic bank generations/exhaustion, callback-only
enqueue leases and dedicated generated header guards. Their permanent regressions
remain part of run_tests.py.

## Boundary agreed during this increment

The subscription layer stores **validated observations**: only an actual update
currently being delivered by the bound producer can enter the store. Catalog,
layout, source applicability and generation must match before retrieval converts
raw bits using metadata. It never accepts a saved/foreign callback update or
silently converts an old-catalog reading using a new catalog.

The application chooses whether to validate age on each read. Existing
latest_get is unchecked for time but always validates metadata/identity/source.
latest_get_with_policy takes an optional caller-owned freshness policy. NULL means
exactly the same unchecked read; a non-NULL policy provides now.nanoseconds,
now.clock_id and max_age_ns. No clock, timer, expiration scan or background work
is added. The policy is borrowed for the call and is never retained in the store.

After metadata, generation and stored-data validation, checked reads require:
1. A stored timestamp, else LATEST_NO_TIMESTAMP.
2. The same clock identity, else LATEST_CLOCK_MISMATCH.
3. now >= observation time, else LATEST_TIME_REVERSED.
4. now - observation time <= max_age_ns, else LATEST_STALE.

The age boundary is inclusive. Zero maximum age accepts only equal timestamps;
timestamp zero remains valid. Comparing before subtracting prevents unsigned
underflow; no timestamp+max_age sum is formed, so UINT64_MAX boundaries are safe.
TIME_REVERSED detects now before the observation, not every clock rewind since a
prior getter call; there is no stored previous-now state. Callers remain responsible
for coherent clock identities and for supplying a meaningful current time/age.

Unchecked reads may return missing timestamps or temporally old observations for
application-managed policy. Checked failures leave both output and stored data
unchanged; they do not expire or erase readings. No read can bypass identity or
source applicability. Quality remains Unknown absent an explicit health policy;
age validation neither authenticates time nor proves sensor health.

## Use and selection

```c
j1939_latest_t latest;
j1939_latest_slot_t retained[100]; /* caller-owned; no heap */
/* After successful keys_init; check every return value. */
j1939_latest_init(&latest, &context, retained, 100);

/* In the ONE shared subscription callback: */
int status = j1939_latest_put(&latest, &context, update);
/* FULL means a new key cannot be retained; existing values are unchanged. */

/* Outside callbacks, using the actually active context/binding: */
j1939_latest_value_t value;
status = j1939_latest_get(&latest, active, &active->identity,
    SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED, domain, source, &value);
if (status == 0) {
    /* Unchecked for time: application manages age/health. */
}
```

```c
j1939_latest_freshness_t policy = { { now_ns, clock_id }, 500000000 };
status = j1939_latest_get_with_policy(&latest, active, &active->identity,
    SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED, domain, source, &policy, &value);
/* status==0 permits use; STALE/NO_TIMESTAMP/CLOCK_MISMATCH/TIME_REVERSED
   must be handled without consuming value. NULL policy selects unchecked time. */
```

The dashboard example includes a possible generated j1939_get_EngineSpeed wrapper
with a fixed catalog-scoped ID. It uses the same generic getter as other signals;
no separate callback per signal is needed. It prints the original four event
observations plus four on-demand latest results. The generated-wrapper sketch accepts an optional policy. Checked dashboard reads
return the two timestamped values and report NO_TIMESTAMP for the other two,
without printing value fields on failure. A NULL-policy wrapper call also tests
application-managed unchecked access. Its host JSON remains an example, not a network
protocol. WebSocket/browser/threading/clock-age policy stay outside the core.

Keys include domain, source identity and generated signal ID. Source means actual
8-bit SA in address mode or logical ECU in logical mode; maps/namespace/layout
are part of the binding. Different domains/sources retain separate entries.
Generic and SA-specific definitions still route through the existing decoder.
Repeated/absent SPNs are not identities. IDs can renumber after regeneration;
externally retained keys need the matching catalog/layout identity. Fingerprints
check consistency, not authentication.

## Event queue versus retained values

The latest store has separate storage. Its entries can be overwritten by later
observations, whereas queued events retain FIFO delivery and old-bank metadata.
They cannot safely share writable record slots. One shared callback may write to
both independently; the application decides how to report either capacity error.
Duplicate registrations of one key coalesce into one latest entry. The last
successfully delivered observation wins by receipt order, not timestamp order.
No automatic eviction occurs at capacity: updating an existing key still works,
adding a new one returns FULL without modifying any existing value.

Unsubscribe stops future observations but does not erase previously accepted
readings. Call latest_forget explicitly to retire a key or latest_reset to clear
all readings. Other registrations for that key may subsequently repopulate it.
An inactive mux branch produces no observation and does not overwrite its last
accepted value. Inactivity is not inferred to mean invalidity: detecting a current
branch or age is an application policy. No value is invented before first receipt.

On a catalog/layout/map change, the application serializes handoff and calls
latest_reset with the new active context. This advances the store generation and
clears all entries. Before reset, getters given the new active context return
MISMATCH and expose no old value, even if keys/IDs are identical. The application
must pass its actually active context (e.g. pair.producers[pair.active]); the store
cannot infer an external activation decision. Queue draining is independent:
old events continue to resolve with their retained old bank/catalog until their
consumer callback finishes. Resetting latest does not free or reset an event bank.

## Results, conversion and validation

- OK (0): copied value returned.
- LATEST_NO_VALUE (1): valid selection has not been retained (or was forgotten).
- KEY_MISMATCH: store/active/expected identity or entry generation mismatch.
- SUB_INVALID: invalid inputs, source applicability or stored raw/frame/flags.
- KEY_MAPPING_MISSING: logical source not mapped.
- LATEST_STALE / LATEST_NO_TIMESTAMP / LATEST_CLOCK_MISMATCH /
  LATEST_TIME_REVERSED: optional time validation failed, as defined above.
- SUB_BUSY: getter/reset/forget attempted while the active producer is delivering.
- SUB_FULL: new entry exceeds capacity; SUB_ID_LIMIT: reset generation exhausted.

All failures leave getter output unchanged; callers must check status and must
not reuse an earlier output on failure. Invalid signal IDs are INVALID rather
than NO_VALUE. Caller storage is private to the API; modifying it is unsupported,
although obvious bad generation/raw width/CAN routing/time-presence/quality flags
are checked on reads. There is no corruption authentication or arbitrary-memory
safety guarantee for forged pointers/metadata. Trusted immutable generated tables
are assumed, as in the existing prototype.

The raw uint64 retains the exact wire pattern. For signed signals signed_raw is
computed without signed overflow, including INT64_MIN; it is meaningful only when
is_signed is true. Engineering uses the matching decoder's binary64 scale/offset.
Integers wider than 53 significant bits can lose engineering precision; exact raw
bits remain available. Nonfinite conversion is explicitly marked by
engineering_finite==0, without inventing a quality classification. Unit is borrowed
from the immutable generated string pool: empty means known empty; NULL means
unavailable. Returned numbers/timestamps are copied, not borrowed slot references.

## Ownership and lifecycle

Store/slots/producer/catalog/maps are caller-owned. No heap, DBC parse, timer,
RTOS, network or background thread is used. Every operation needs external
serialization with callbacks, bank changes and metadata retirement. Busy flags
are reentrancy guards, not synchronization primitives. Put is callback-only and
accepts the exact current update pointer; copies reject before any mutation.
Getters and reset/forget are used outside callbacks. Callbacks must return normally.

Initialization creates a fresh incarnation at generation 1 and clears slots;
invalid initialization does not modify either slots or store. Before reusing
storage, retire all old borrowed unit pointers/results and quiesce all users.
Use reset for a live clear/rebind. It increments generation, clears slots and
rejects UINT64_MAX transactionally. Retired generations are not reused within an
incarnation. There is no guarantee across arbitrary storage reuse or overwritten
immutable catalogs. An identical binding can intentionally retain observations
through a producer restart; explicitly reset when that is not desired.

Unit pointers remain valid while their catalog is retained, but the caller must
finish/copy them before reset or metadata retirement. No asynchronous lease is
provided. Queue bank ownership rules remain unchanged: a draining bank cannot be
reused until its consumer callback and all permitted borrows have ended. Shutdown
requires quiescence, explicit drain/abandonment, and retirement of all borrowed
strings before storage/catalog release.

## Cost, tests and language APIs

The prototype reuses the unchanged 40-byte observation record and adds an 8-byte
entry generation (zero means free). Descriptors remain once in the catalog.
The record includes CAN ID, token/tag and timestamp context for validation and
compatibility; a future dedicated latest record could remove delivery fields,
but no unmeasured packing change is made here. A store-level binding is shared.
Lookup/write/forget are linear in retained capacity; no throughput promise is made.

Run run_tests.py for compiler generation, existing regression suites, dashboard
and latest_tests.c. Run measure_latest.py --compiler /path/to/dbc_compiler
--build-dir /tmp/latest-measured for a sanitized 100-retained-values/30-PGN
exercise, actual host ABI sizes and Apple-Clang -Os Cortex-M4 object/ABI sizes.
Reports distinguish execution on host from cross-compilation; no target timing,
RTOS execution or firmware linker placement is established.

C++ could expose expected<Value, Error> with a typed ID and immutable catalog
ownership. Python can return a dataclass with arbitrary-precision raw integers;
Go a value struct plus error, raw uint64 and int64 when signed. All should share
ID/binding/source rules, optional timestamp representation, explicit errors,
receipt-order replacement and exact raw semantics. The same optional policy, inclusive boundary, status precedence and uint64
arithmetic can be shared across language implementations, while applications may
choose unchecked reads and implement their own policy. Native runtimes versus
bindings, symbol generation, eviction, unsubscribe-clearing policy, API/ABI and
borrowed string ownership still need review before becoming public contracts.
