/* Optional generated interpretation metadata. The decoder ABI in j1939_db.h
 * is unchanged. These native C structures are not a serialized wire format. */
#ifndef J1939_DB_METADATA_H
#define J1939_DB_METADATA_H
#include "j1939_db.h"

#define J1939_DB_METADATA_VERSION 1u
enum {
    J1939_META_HAS_SPN = 1u,
    J1939_META_HAS_DEFAULT = 2u,
    J1939_META_DEFAULT = 4u
};

typedef struct j1939_signal_metadata {
    uint32_t unit_off;
    uint32_t scale_off;
    uint32_t offset_off;
    uint32_t minimum_off;
    uint32_t maximum_off;
} j1939_signal_metadata_t;

typedef struct j1939_source_metadata {
    uint32_t name_off;
    uint32_t version_off; /* J1939_NO_NAME when VERSION is absent */
    uint64_t byte_length;
    uint64_t fnv1a64;     /* provenance checksum, NOT authentication */
} j1939_source_metadata_t;

typedef struct j1939_signal_definition {
    uint32_t name_off;
    uint32_t line;
    uint32_t spn;
    uint16_t signal_index; /* global index into db->signals and signal metadata */
    uint16_t flags;        /* HAS_SPN distinguishes absent from explicit zero */
} j1939_signal_definition_t;

typedef struct j1939_message_definition {
    uint32_t dbc_id;       /* original BO_ ID, including extended flag */
    uint32_t name_off;
    uint32_t line;
    uint32_t spn;
    uint32_t signal_begin; /* slice of signal_definitions */
    uint16_t source_index;
    uint16_t message_index;
    uint16_t signal_count;
    uint16_t flags;
} j1939_message_definition_t;

typedef struct j1939_db_metadata {
    uint32_t version;
    const j1939_db_t *db;
    const j1939_signal_metadata_t *signals; /* same order/count as db->signals */
    const j1939_source_metadata_t *sources;
    const j1939_message_definition_t *message_definitions;
    const j1939_signal_definition_t *signal_definitions;
    uint32_t source_count;
    uint32_t message_definition_count;
    uint32_t signal_definition_count;
    uint32_t strings_size;
} j1939_db_metadata_t;

/* Trusted generated metadata only. Returned strings share the database's const
 * string pool and live as long as that database. NULL means unavailable/invalid;
 * a non-NULL pointer to "" means an explicitly empty DBC string. */
static inline const char *j1939_metadata_text(const j1939_db_metadata_t *meta,
                                             uint32_t offset) {
    if (!meta || meta->version != J1939_DB_METADATA_VERSION || !meta->db ||
        !meta->db->strings || offset >= meta->strings_size) return NULL;
    return meta->db->strings + offset;
}

static inline const char *j1939_signal_unit(const j1939_db_metadata_t *meta,
                                           uint16_t signal_index) {
    if (!meta || meta->version != J1939_DB_METADATA_VERSION || !meta->db ||
        !meta->signals || signal_index >= meta->db->signal_count) return NULL;
    return j1939_metadata_text(meta, meta->signals[signal_index].unit_off);
}
#endif
