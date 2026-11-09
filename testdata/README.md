# Test data

Real NMEA 2000 / J1939 captures used as test fixtures, mainly for address-claim handling
(PGN 60928) and SA ↔ NAME tracking. They are read with `ActisenseStream`
([can_stream/actisense_stream.h](../can_stream/actisense_stream.h)), and the tests are in
[tests/canboat_fixture_tests.cpp](../tests/canboat_fixture_tests.cpp).

The source is the `samples/` directory of [canboat](https://github.com/canboat/canboat),
pinned to commit `ee93a6b9dc2aa6e939e4e95bc925e4841dad98e7` (2026-11-09).

## Committed excerpts: `canboat/`

These are small verbatim line ranges, a few hundred lines each, cut from the full captures.
The tests that use them always run. Each file starts with `#` comment lines that give its
source file, line range, and commit.

| File | Source (lines) | What it shows |
|---|---|---|
| `live_claim_cascade.all` | `live.all` (54941–55240) | An arbitration cascade with both outcomes. NAME `0xc07887002fb83d30` claims SA 17 and loses to the lower NAME holding it. It then tries 18, 19 and so on up to 22, losing each time. At SA 23 it wins against a higher NAME, which moves to 24 and starts a chain of moves up to SA 31. |
| `nac3_reset_reclaim.raw` | `nac3-reset-dockside-commissioning.raw` (2061–2340) | After an autopilot reset, the whole bus re-claims. Five NAMEs contend for SA 0–5 within about 0.5 s. |
| `navico_name_walk.raw` | `navico-source-selection.raw` (10640–10960) | Lower NAMEs push NAME `0xc0509bff2270dbfb` from SA 4 to 5, 7, 8, 11 and 15. |
| `dirona_plain_claims.raw` | `dirona-actisense-serial.raw` (3001–3200) | Plain actisense-serial output, without the `# format=FAST` header. It contains address claims, reassembled fast-packet messages (129029 arrives as one 43-byte line), and one canboat pseudo-PGN line that must be skipped. |

None of the canboat samples contain a Cannot Claim (a claim from SA 254) or a Commanded
Address (PGN 65240). Those cases need synthetic tests.

The excerpts are © Kees Verruijt and canboat contributors, distributed under the Apache
License 2.0. `canboat/LICENSE` is canboat's own license notice, copied unchanged.

## Full captures: `external/` (not committed, gitignored)

```bash
scripts/fetch_test_logs.sh            # sparse, shallow fetch of samples/ only (~216 MB)
```

The script fetches exactly the pinned commit and checks out only `samples/` and `LICENSE`.
Running it again does nothing if that commit is already checked out. The `CanboatFullLog*`
tests replay six of the captures, about 1.8 M messages in total. They compare the results
with message, claim and timestamp counts that a separate Python reader produced from the
same files. The `NodeTableReplayTest.*FullCapture` tests replay captures through the node
table. When the files aren't there, the tests skip and print the file name and the fetch
command. Set `CANBOAT_SAMPLES_DIR` to use a checkout somewhere else.

Set `J1939_FULL_LOGS_REQUIRED=1` to make a missing file a test failure instead of a skip. The
full-log CI job sets it, so a broken fetch can't pass having tested nothing.

`testdata/external` is also excluded from the Docker build context. To run the full-log
tests in the container, mount the samples into it:

```bash
docker run --rm -v "$PWD/testdata/external:/src/testdata/external:ro" -e J1939_FULL_LOGS_REQUIRED=1 \
    j1939 build/bin/all_tests --gtest_filter='Canboat*:NodeTableReplay*'
```

If you bump the pinned commit, recompute the expected numbers in the tests at the same time.
