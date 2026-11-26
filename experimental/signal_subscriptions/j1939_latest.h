/* Experimental allocation-free latest observations, separate from event FIFO. */
#ifndef EXPERIMENTAL_J1939_LATEST_H
#define EXPERIMENTAL_J1939_LATEST_H
#include "j1939_key_queue.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { J1939_LATEST_NO_VALUE=1, J1939_LATEST_STALE=2,
    J1939_LATEST_NO_TIMESTAMP=3, J1939_LATEST_CLOCK_MISMATCH=4,
    J1939_LATEST_TIME_REVERSED=5 };
typedef struct {
    j1939_sub_timestamp_t now;
    uint64_t max_age_ns; /* inclusive; zero accepts only timestamp == now */
} j1939_latest_freshness_t;
typedef struct {
    j1939_key_record_t record; /* no descriptor; most recent delivered observation */
    uint64_t generation; /* zero is a free slot */
} j1939_latest_slot_t;
typedef struct {
    const j1939_key_context_t *producer;
    const j1939_key_catalog_t *catalog;
    j1939_key_identity_t identity;
    j1939_latest_slot_t *slots;
    uint64_t generation;
    uint16_t capacity;
} j1939_latest_t;
typedef struct {
    uint64_t raw;
    int64_t signed_raw; /* valid iff is_signed; raw always retains exact bits */
    double engineering;
    const char *unit; /* borrowed from immutable catalog; NULL unavailable, "" empty */
    j1939_sub_timestamp_t timestamp;
    uint8_t has_timestamp,is_signed,quality,engineering_finite;
} j1939_latest_value_t;
/* Fresh incarnation only: retire all old borrows before reusing object/storage.
 * Caller owns disjoint slots; metadata and producer live/immutable while bound.
 * Init clears slots; invalid init leaves object and slots unchanged. */
int j1939_latest_init(j1939_latest_t *,const j1939_key_context_t *,j1939_latest_slot_t *,uint16_t);
/* Explicit live reset/rebind: clears slots, advances generation, rejects exhaustion.
 * Does not touch subscriptions or event queues. Retire borrowed units first. */
int j1939_latest_reset(j1939_latest_t *,const j1939_key_context_t *active);
/* Only the exact currently delivered callback update can be stored. Rejects
 * before mutation, including FULL (no eviction), mismatched catalog and copies. */
int j1939_latest_put(j1939_latest_t *,const j1939_key_context_t *,const j1939_key_update_t *);
/* Explicit signal ID/domain/source (actual SA or logical ECU per active binding).
 * expected and active must match the store BEFORE any slot interpretation.
 * Values are valid observations, not a promise of temporal freshness. Timestamp
 * interpretation/age policy belongs to the application. Output unchanged on all
 * failures. Getter is not a pop.
 * Units remain borrowed until metadata retirement; serialize getters/put/reset
 * and callbacks externally. During producer callback getter/reset return BUSY. */
int j1939_latest_get(const j1939_latest_t *,const j1939_key_context_t *active,
    const j1939_key_identity_t *expected,uint32_t signal_id,uint32_t domain,uint32_t source,
    j1939_latest_value_t *out);
/* Optional read-time validation, after all identity/storage/source checks.
 * NULL policy is exactly the unchecked get above. Non-NULL policy requires a
 * timestamp on the same clock, now >= timestamp and age <= max_age_ns.
 * Checks never expire/erase slots or modify output on failure. Policy is borrowed
 * only during this call; no per-store/entry RAM or timer dependency is added. */
int j1939_latest_get_with_policy(const j1939_latest_t *,const j1939_key_context_t *active,
    const j1939_key_identity_t *expected,uint32_t signal_id,uint32_t domain,uint32_t source,
    const j1939_latest_freshness_t *policy,j1939_latest_value_t *out);
/* Unsubscribe does not erase last observations: explicitly forget or reset. */
int j1939_latest_forget(j1939_latest_t *,const j1939_key_context_t *active,
    const j1939_key_identity_t *expected,uint32_t signal_id,uint32_t domain,uint32_t source);
#ifdef __cplusplus
}
#endif
#endif
