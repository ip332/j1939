/* EXPERIMENTAL issue-22 prototype. Native structures and IDs are not a stable ABI. */
#ifndef EXPERIMENTAL_J1939_SUBSCRIPTIONS_H
#define EXPERIMENTAL_J1939_SUBSCRIPTIONS_H
#include "j1939_db_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    J1939_SUB_OK = 0, J1939_SUB_NOT_FOUND = 1,
    J1939_SUB_INVALID = -1, J1939_SUB_UNSUPPORTED = -2,
    J1939_SUB_FULL = -3, J1939_SUB_BUSY = -4, J1939_SUB_ID_LIMIT = -5
} j1939_sub_result_t;
typedef enum {
    J1939_SUB_DECLARED_SOURCE = 0,
    J1939_SUB_ANY_ROUTED_SOURCE = 1
} j1939_sub_source_policy_t;
/* Bundle-local selection. Never serialize indices/pointers as a stable signal ID. */
typedef struct {
    const j1939_db_metadata_t *metadata;
    uint32_t message_definition, signal_definition;
} j1939_sub_ref_t;
typedef struct {
    uint64_t nanoseconds;
    uint32_t clock_id; /* caller's clock domain, no epoch or monotonicity inferred */
} j1939_sub_timestamp_t;
typedef struct {
    uint32_t can_id; /* raw 11/29-bit ID, no SocketCAN EFF/RTR/ERR flags */
    const uint8_t *data; /* at least size readable bytes, immutable during process */
    const j1939_sub_timestamp_t *timestamp; /* NULL means absent; zero is valid */
    uint8_t size, extended; /* classic CAN only, extended exactly 0 or 1 */
} j1939_sub_frame_t;
enum { J1939_SUB_QUALITY_UNKNOWN = 0 };
typedef struct {
    uint32_t subscription_id, tag;
    j1939_sub_ref_t selection;
    uint32_t can_id, pgn;
    uint64_t raw; /* exact zero-extended raw wire bit pattern, including signed signals */
    int64_t signed_raw; /* meaningful only if is_signed; exact, including INT64_MIN */
    double engineering;
    const char *unit; /* borrowed const database string; "" is a known empty unit */
    const j1939_signal_t *signal; /* width, enums, scale/offset, mux, signedness */
    const j1939_signal_metadata_t *interpretation; /* exact decimal strings via offsets */
    j1939_sub_timestamp_t timestamp; /* meaningful only if has_timestamp */
    uint8_t source_address, has_timestamp, is_signed, quality, engineering_finite;
} j1939_sub_update_t;
typedef void (*j1939_sub_callback_t)(void *user, const j1939_sub_update_t *update);
/* Caller supplies slots; do not inspect/mutate them while context is live. */
typedef struct {
    j1939_sub_ref_t selection;
    uint32_t id, tag; /* id==0 is free */
    uint16_t message_index, signal_index, source; /* source=256 means any routed SA */
} j1939_sub_slot_t;
typedef struct {
    const j1939_db_metadata_t *metadata;
    j1939_sub_slot_t *slots;
    j1939_sub_callback_t callback;
    void *user;
    uint32_t next_id;
    uint16_t capacity;
    uint8_t busy;
} j1939_sub_context_t;

/* Only trusted compiler-generated metadata-v1. Input is not authenticated.
 * Failed init leaves ctx untouched. Capacity 0 permits NULL slots. Storage,
 * metadata and callback user must outlive ctx. Do not re-init from a callback. */
int j1939_sub_init(j1939_sub_context_t *ctx, const j1939_db_metadata_t *metadata,
                   j1939_sub_slot_t *slots, uint16_t capacity,
                   j1939_sub_callback_t callback, void *user);
/* Exact case-sensitive names, including logical .dbc source basename.
 * Output unchanged on failure. This is metadata lookup, not DBC parsing. */
int j1939_sub_select(const j1939_sub_context_t *ctx, const char *source,
                     const char *message, const char *signal, j1939_sub_ref_t *out);
/* Multiple registrations of one selection are allowed. One shared context
 * callback receives caller tags and unique context-lifetime nonzero tokens.
 * At UINT32_MAX IDs are exhausted instead of wrapping/reusing stale tokens. */
int j1939_subscribe(j1939_sub_context_t *ctx, j1939_sub_ref_t selection,
                    j1939_sub_source_policy_t policy, uint32_t tag, uint32_t *id);
int j1939_unsubscribe(j1939_sub_context_t *ctx, uint32_t id);
/* Every active observation, synchronously, ascending slot order. No callbacks
 * for unknown IDs/inactive branches. Matched short frames reject as a whole.
 * out_count optional; written only on success. No latest-value cache or queue.
 * Calls on the same ctx during a callback return BUSY. No internal locking. */
int j1939_sub_process(j1939_sub_context_t *ctx, const j1939_sub_frame_t *frame,
                      uint16_t *out_count);
#ifdef __cplusplus
}
#endif
#endif
