# Experimental J1939 → browser signal lab

A finite, deterministic end-to-end replay on the experimental subscription API.
This is not a CAN driver, production gateway, stable transport or BRF change.
All frames and changed catalog definitions are redistributable synthetic data.

## Build, run and stop

Built on the experimental subscription API at commit41f357d. Prerequisites:
C11 and C++14 compilers (GCC or Clang), Python3, protoc, Python protobuf available
in MODEL_PYTHON, and working AddressSanitizer/UBSan runtimes. Node.js18+ is needed
only for `--test` (browser-model tests); a modern browser needs ES modules, BigInt,
fetch, AbortController and EventSource. No Python HTTP dependency, npm packages or CDN is required.
On macOS use explicit CC=gcc-16/CXX=g++-16 to select GCC rather than Apple's gcc alias.

From the repository root:

```sh
export MODEL_PYTHON=/path/to/protobuf-venv/bin/python
CC=clang CXX=clang++ python3 experimental/signal_subscriptions/web/run.py \
  --build-dir /tmp/j1939-web --port 8765
```

The default command first builds/runs the existing prototype suite from this
checkout, then generates both C databases and ID catalogs, compiles/runs the
sanitized C producer and starts a server. Open **http://127.0.0.1:8765/**.
**Ctrl-C** in that terminal stops the bridge. Close the tab to stop its stream.
Nothing is installed or exposed on non-loopback interfaces. Choose `--port 0`
for an available port (the actual URL is printed). `--delay 0.12` controls replay
pacing per event, not CAN timestamps. Press Replay in the page to start a new
independent session. EventSource closes on done/error, avoiding automatic duplicate
replays. Each client receives the same finite replay from the start.

To reuse an already built compiler from the same source base:

```sh
MODEL_PYTHON=/path/to/protobuf-venv/bin/python CC=clang \
  python3 experimental/signal_subscriptions/web/run.py --build-dir /tmp/j1939-web \
  --compiler /tmp/subs-clang/dbc_compiler --port 8765
```

The optional explicit compiler skips the initial full suite; it does not introduce
runtime DBC parsing. It must support the committed strict-model/opt-in-metadata CLI.
Build products, manifests, exact compiler commands, generated fixture header,
catalogs.json and events.json live only in the requested build directory.
Assertions are intentionally enabled (`-UNDEBUG`) in the host demonstration harness.

## What you see

One shared C callback retains9 selected signals (data-driven registration table)
from4 message/source routes: EngineA at SA11 and source-specific EngineB at SA62
share PGN0xff01; Pressure at SA11 uses0xff02; WideUnsigned at SA11 uses0xff03.
There are3 unique PGNs,2 source addresses and domain1. Add table rows and provision
capacity to extend the example; it does not promise100-signal throughput. Existing
prototype measurement scripts separately exercise100 subscriptions/30 PGNs.

Initial independent expectations include EngineA.Speed=1000 rpm (raw8000), signed
Temperature=-41 deg C (raw254, signed-2), mux1 Load=50% (raw125), EngineB.Speed=2000
rpm and Motorola Pressure=95 kPa (raw50). Status255 retains Unknown quality; its
reserved enum label is not a health policy. WideUnsigned raw is exactly
`18446744073709551615`; its binary64 engineering value is approximate and may be
rendered differently by JavaScript. Raw and signed raw are decimal strings/BigInt,
never exactness claims about JavaScript Number. Empty units remain empty strings.

Catalog B keeps the same definition names/IDs/packed keys but changes generic
EngineA/alias Speed from factor0.125/rpm to factor0.25/"demo rpm". This fictional
unit/scale change is only a consistency exercise. A raw8000 key now means2000 demo
rpm. The producer switches to B with old A events still queued, rejects an old-
store/new-context latest read, and reports BUSY for a third switch. It drains all
old events using A metadata before B events. Gray archived rows remain1000 rpm;
active B rows show2000 demo rpm. No old event is interpreted with B's descriptors.

A clock event advances the explicitly simulated nanosecond clock7 from1.4 to1.5
seconds, then3.000000001 seconds. It is not UTC or wall time and is not inferred
from browser receipt. Active rows become stale at the1-second maximum-age limit;
the page hides stale engineering values and shows an em dash. Raw/time fields
remain clearly historical. Archived rows keep their old interpretation, not current
freshness. Core latest_get_with_policy returns0 for the fresh test and2 for stale.

Finally a separate deliberate overload sends20 updates into an empty16-record
queue and reports4 drop-newest observations. Latest storage still receives those
valid observations independently. The tested normal handoff drops0; no lossless
claim applies beyond these conditions. Partial drain, queue counters, BUSY and
wrong-catalog statuses are asserted and displayed.

## Boundary, identity and transport

C producer.c uses only generated C metadata/IDs plus caller-owned contexts,
registration arrays, two16-record queue banks and two9-value latest stores. There
is one callback, not one per signal. JSON formatting/stdio is a **host adapter**:
stdio can allocate internally. The embedded decoder/subscription/queue/latest
modules require no heap, network, DBC parser, background thread or RTOS. Python
and the browser allocate normally. Generation/protobuf/DBC parsing run only on host.

The Python stdlib loopback HTTP bridge forwards the C producer's JSON as SSE.
It buffers the finite replay in host memory and is not a concurrent C queue.
The C producer finishes before HTTP starts: it serializes values while both native
banks are alive; the bridge retains JSON and generated manifest copies, not C
pointers. A live CAN adapter replacing the fixture loop is not implemented here. No
WebSocket is needed for one-way updates. Endpoints are /catalogs, /events and
three static assets, plus /. Unknown paths return404. There is no authentication,
TLS, remote configuration or CAN ingress endpoint; do not infer production gateway
security from loopback binding or metadata hashes.

Each activation binds generation + catalog SHA + layout/version + source policy/
namespace/map fingerprint. Each observation identifies generation/catalog and key.
The browser retains both manifests and resolves signal ID in the matching one,
checks packed fields/unit/PGN/raw/signed shape, and keys rows by full association
plus key. A key alone, SPN alone or ID alone does not identify an interpretation.
The host rejects unknown associations, bad packed fields and Number-valued raw64.
Fingerprints check accidental consistency, not authentication against rewritten data.
Catalog lifetime/pair ownership and core externally serialized use remain unchanged.

## Automated validation

```sh
MODEL_PYTHON=/path/to/protobuf-venv/bin/python CC=clang CXX=clang++ \
  python3 experimental/signal_subscriptions/web/run.py --build-dir /tmp/web-clang --test
MODEL_PYTHON=/path/to/protobuf-venv/bin/python CC=gcc-16 CXX=g++-16 \
  python3 experimental/signal_subscriptions/web/run.py --build-dir /tmp/web-gcc --test
```

`--test` does not leave a server running. It compares independently specified raw,
engineering, signed, unit, source and timestamp expectations, checks mux inactivity,
reused-key catalog handoff, stale/BUSY/drop counters, rejects changed binding/key/
unit/raw types, starts an ephemeral HTTP server, verifies /catalogs and SSE output,
and runs the same browser model under Node. C production executes with fail-fast
ASan/UBSan. The browser model expects18 separate old/new rows and38 observations:
13 old +9 new in normal handoff, then16 retained overload observations. The4 dropped
observations are counted, not silently presented as delivered.

The page is also suitable for headless Chrome DOM/screenshot checks. Automated
headless rendering is not a claim of interactive manual browser testing, hardware,
target execution, RTOS, throughput or concurrent producer/consumer validation.
The producer prints host sizeof totals for the explicit contexts/slots/banks/
metadata/latest storage/pair/callback state, excluding stack temporaries, generated
ROM, stdio/Python/browser/network memory and final linker placement. No Cortex-M4
application footprint or cycle timing is inferred from the host.

## Replay session ownership

Every Replay retires the previous session before awaiting catalog data: it aborts
that session's fetch and closes its own EventSource. A per-session identity check
runs after both fetch and JSON-body awaits, and before every message/error handler.
Abort is not assumed to win a race: even a late successful response/body or failure
cannot create a source or alter the current view. Handlers capture their own model
and EventSource; no mutable global model/stream is shared across sessions.

Replay immediately clears previous rows/diagnostics and done/error markers, showing
Connecting until valid events arrive. Normal completion closes that session's
source and retains its completed view. Current disconnect, rejected event or startup
failure closes only its own source and clears values to Data unavailable with a
specific failure banner; Replay can recover. Late events/errors after completion,
disconnect or replacement are ignored. Automatic reconnect is not used; Replay
starts a new independent finite session.

`--test` now runs test_controller.mjs against the actual dashboard controller in a
deterministic DOM/fetch/EventSource harness. The transport deliberately ignores
abort so token checks must prevent late results. It covers out-of-order catalog
responses/bodies, late success/failure, the reported stale-to-fresh injection,
saved old handlers, repeated clicks, ownership of done/error cleanup, disconnect,
HTTP/JSON/model/source-creation/protocol failures and normal replay recovery.
These are automated controller tests, not proof of behavior in every browser.
