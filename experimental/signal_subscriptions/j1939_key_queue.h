/* Optional single-owner queue: signal descriptors live in ONE metadata blob. */
#ifndef EXPERIMENTAL_J1939_KEY_QUEUE_H
#define EXPERIMENTAL_J1939_KEY_QUEUE_H
#include "j1939_signal_keys.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const j1939_key_context_t *producer;
    j1939_key_identity_t identity; /* one immutable binding snapshot per blob */
} j1939_key_queue_metadata_t;
/* No descriptors, unit/scale/enum pointers or identity copies per observation.
 * Signal ID in packed_key resolves everything through the queue metadata. */
typedef struct {
    uint64_t raw, nanoseconds;
    uint32_t packed_key, can_id, clock_id, subscription_id, tag;
    uint8_t has_timestamp, quality;
} j1939_key_record_t;
typedef struct {
    const j1939_key_queue_metadata_t *metadata;
    j1939_key_record_t *records;
    uint64_t generation;
    uint32_t dropped, invalidated;
    uint16_t capacity, read_index, count;
    uint8_t stale;
} j1939_key_queue_t;
int j1939_key_queue_metadata_init(j1939_key_queue_metadata_t *blob,const j1939_key_context_t *producer);
/* Fresh-incarnation initialization only: all previous record tokens/borrows for
 * this object must already be retired. Starts at generation 1; cannot detect
 * tokens from a prior incarnation or a different queue. Use replace for live
 * reset (flush + advancing generation). Never init a live pair-owned queue. */
int j1939_key_queue_init(j1939_key_queue_t *queue,const j1939_key_queue_metadata_t *blob,
                         j1939_key_record_t *records,uint16_t capacity);
/* Stop/serialize producer and consumer before replacement. Always flushes,
 * advances generation, and invalidates old popped-record generation tokens,
 * even if replacement metadata has an identical fingerprint. */
int j1939_key_queue_replace(j1939_key_queue_t *queue,const j1939_key_queue_metadata_t *blob);
/* Only the exact update pointer during the bound producer's current synchronous
 * callback is accepted. Copies/saved updates reject with MISMATCH before mutation.
 * Callback must return normally; longjmp is unsupported. Full => drop newest +
 * count; no blocking. This lease is a lifecycle check, not authentication. */
int j1939_key_queue_push(j1939_key_queue_t *queue,const j1939_key_context_t *producer,
                         const j1939_key_update_t *update);
/* Consumer only after process returns, externally serialized. Generation is
 * returned separately, not stored in each record. Outputs unchanged on failure. */
int j1939_key_queue_pop(j1939_key_queue_t *queue,j1939_key_record_t *record,uint64_t *generation);
/* The UNIQUE signal ID looks up the original descriptor. Rejects old generation
 * before interpreting the key. The metadata/descriptor pointers are borrowed. */
int j1939_key_queue_descriptor(j1939_key_queue_t *queue,uint64_t generation,
    const j1939_key_record_t *record,j1939_sub_ref_t *selection);
/* Experimental two-generation handoff. Queues, contexts, descriptors and slot
 * arrays remain caller-owned. All calls externally serialized at frame boundaries.
 * Do not mutate/process component queues directly while owned by the pair;
 * prepare an inactive bank only after reusable succeeds, using replace rather
 * than queue_init to preserve its generation. Consume through drain: callback must finish interpretation/copying before
 * returning and must not retain borrowed descriptors. No asynchronous leases. */
typedef struct {
    j1939_key_queue_t *queues[2];
    j1939_key_context_t *producers[2];
    uint64_t generation;
    uint8_t active, has_draining, busy;
} j1939_key_pair_t;
typedef void (*j1939_key_consumer_t)(void *,j1939_key_queue_t *,const j1939_key_record_t *,uint64_t);
/* Fresh pair object only. Adopts max(bank generations); exhaustion rejects
 * transactionally. Bank objects/storage must be disjoint and retain addresses. */
int j1939_key_pair_init(j1939_key_pair_t *pair,j1939_key_queue_t *first,j1939_key_context_t *first_producer,
    j1939_key_queue_t *second,j1939_key_context_t *second_producer);
/* Ask before preparing/reinitializing an inactive bank. BUSY => retain it intact. */
int j1939_key_pair_reusable(const j1939_key_pair_t *pair,uint8_t *bank);
/* Prepared inactive bank must be empty/valid; BUSY if prior generation remains.
 * Never flushes queued data. Third update must wait, not overwrite a draining bank. */
int j1939_key_pair_switch(j1939_key_pair_t *pair);
int j1939_key_pair_process(j1939_key_pair_t *pair,uint32_t domain,const j1939_sub_frame_t *frame,uint16_t *count);
/* FIFO across transition: finish old generation first, then active. Partial
 * counts are returned on errors; callback completion releases each record. */
int j1939_key_pair_drain(j1939_key_pair_t *pair,uint16_t limit,j1939_key_consumer_t consume,void *user,uint16_t *count);
#ifdef __cplusplus
}
#endif
#endif
