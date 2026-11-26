/* Experimental packed identities. No stable ABI or authenticated wire format. */
#ifndef EXPERIMENTAL_J1939_SIGNAL_KEYS_H
#define EXPERIMENTAL_J1939_SIGNAL_KEYS_H
#include "j1939_subscriptions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define J1939_KEY_VERSION 1u
enum { J1939_KEY_MISMATCH=-6, J1939_KEY_MAPPING_MISSING=-7, J1939_KEY_AMBIGUOUS=-8 };
typedef struct { uint8_t domain_bits, source_bits, signal_bits; } j1939_key_layout_t;
typedef struct { uint32_t message_definition, signal_definition; } j1939_key_entry_t;
typedef struct {
    const j1939_db_metadata_t *metadata;
    const j1939_key_entry_t *entries; /* ID is array index: zero is VALID */
    uint32_t count, version;
    uint64_t metadata_hash, entry_hash;
    uint8_t sha256[32]; /* host-generated catalog fingerprint, NOT authentication */
} j1939_key_catalog_t;
typedef enum { J1939_KEY_SOURCE_ADDRESS=0, J1939_KEY_LOGICAL_ECU=1 } j1939_key_source_mode_t;
typedef struct { uint32_t domain, logical_ecu, address; /* address validated as0..255 */ } j1939_key_mapping_t;
typedef struct {
    uint8_t catalog_sha256[32];
    uint64_t mapping_hash, source_namespace;
    uint32_t version;
    j1939_key_layout_t layout;
    uint8_t source_mode;
} j1939_key_identity_t;
/* Explicit flags: no field value is reserved. Unused wildcard fields must be0. */
typedef struct { uint32_t domain, source; uint8_t any_domain, any_source; } j1939_key_filter_t;
typedef struct { j1939_key_filter_t filter; uint32_t signal_id, tag; } j1939_key_slot_t;
typedef struct {
    j1939_sub_update_t value;
    uint32_t packed_key, domain, source_identity, signal_id;
} j1939_key_update_t;
typedef void (*j1939_key_callback_t)(void *,const j1939_key_update_t *);
typedef struct {
    j1939_sub_context_t base;
    const j1939_key_catalog_t *catalog;
    const j1939_key_mapping_t *mapping;
    j1939_key_slot_t *key_slots;
    const j1939_key_update_t *current_update; /* internal callback-only enqueue lease */
    j1939_key_callback_t callback;
    void *user;
    j1939_key_identity_t identity;
    uint32_t mapping_count, current_domain, current_source;
    uint16_t delivered;
    uint8_t busy;
} j1939_key_context_t;
int j1939_key_identity_equal(const j1939_key_identity_t *a,const j1939_key_identity_t *b);
int j1939_key_layout_make(uint32_t domain_bits,uint32_t source_bits,uint32_t signal_bits,j1939_key_layout_t *out);
int j1939_key_layout_valid(j1939_key_layout_t layout);
int j1939_key_pack(j1939_key_layout_t layout,uint32_t domain,uint32_t source,uint32_t signal,uint32_t *out);
int j1939_key_unpack(j1939_key_layout_t layout,uint32_t key,uint32_t *domain,uint32_t *source,uint32_t *signal);
/* Trusted generated metadata only. Canonical field hashing excludes pointers/padding. */
uint64_t j1939_key_metadata_hash(const j1939_db_metadata_t *metadata);
/* Mapping immutable, sorted by (domain,address), with unique pairs. Init is
 * transactional; source_namespace must be nonzero in logical mode, zero for SA.
 * Legacy/key storage arrays are caller-owned and capacity-sized. */
int j1939_keys_init(j1939_key_context_t *ctx,const j1939_key_catalog_t *catalog,const uint8_t expected_catalog_sha256[32],
    j1939_key_layout_t layout,j1939_key_source_mode_t mode,uint64_t source_namespace,
    const j1939_key_mapping_t *mapping,uint32_t mapping_count,
    j1939_sub_slot_t *base_slots,j1939_key_slot_t *key_slots,uint16_t capacity,
    j1939_key_callback_t callback,void *user);
int j1939_keys_subscribe(j1939_key_context_t *ctx,const j1939_key_identity_t *expected,
    uint32_t signal_id,j1939_key_filter_t filter,uint32_t tag,uint32_t *token);
int j1939_keys_subscribe_key(j1939_key_context_t *ctx,const j1939_key_identity_t *expected,
    uint32_t key,uint32_t tag,uint32_t *token);
int j1939_keys_unsubscribe(j1939_key_context_t *ctx,uint32_t token);
int j1939_keys_process(j1939_key_context_t *ctx,uint32_t domain,const j1939_sub_frame_t *frame,uint16_t *count);
/* Check identity BEFORE unpacking/looking up. Explicit envelope required for external keys. */
int j1939_keys_decode(const j1939_key_context_t *ctx,const j1939_key_identity_t *expected,
    uint32_t key,j1939_sub_ref_t *out);
/* SPN is metadata, not identity. Full match count may exceed caller capacity. */
uint32_t j1939_keys_by_spn(const j1939_key_catalog_t *catalog,uint32_t spn,uint32_t *ids,uint32_t capacity);
#ifdef __cplusplus
}
#endif
#endif
