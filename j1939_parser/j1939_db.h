// Plain-C J1939/CAN database. The compiler emits const tables of these structs;
// this header and j1939_db.c are the only code that runs on the target.
// Nothing here allocates, and nothing here has a constructor.

#ifndef J1939_DB_H
#define J1939_DB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    J1939_SIG_SIGNED        = 1u << 0,
    J1939_SIG_LITTLE_ENDIAN = 1u << 1,
    J1939_SIG_MULTIPLEXOR   = 1u << 2,
    J1939_SIG_MULTIPLEXED   = 1u << 3,
    /* Length is in bits, but the value is a byte string rather than a uint64. */
    J1939_SIG_BYTES         = 1u << 4
};

/* name_off sentinel used when the database was built with strings stripped. */
#define J1939_NO_NAME 0xFFFFFFFFu

typedef struct j1939_enum {
    uint32_t value;
    uint32_t name_off;
} j1939_enum_t;

typedef struct j1939_signal {
    uint32_t name_off;
    uint32_t spn;          /* 0 when the DBC has no SPN attribute */
    uint16_t start_bit;    /* DBC bit numbering */
    uint16_t length;       /* bits; may exceed 64 when J1939_SIG_BYTES is set */
    uint16_t enum_begin;   /* index into enums[]; enum_count 0 means no VAL_ */
    uint16_t enum_count;
    uint16_t mux_value;    /* meaningful when J1939_SIG_MULTIPLEXED is set */
    uint16_t flags;
    double scale;
    double offset;
    double min_value;
    double max_value;
} j1939_signal_t;

typedef struct j1939_message {
    uint32_t name_off;
    uint32_t pgn;          /* 0 for an 11-bit frame */
    uint32_t can_id;       /* 11-bit id, or 29-bit id with SA and priority masked off */
    uint32_t sa_mask;      /* 0xFF when this row is specific to one source address, else 0 */
    uint8_t sa;            /* significant only when sa_mask != 0 */
    uint8_t extended;      /* 0 = standard CAN id, 1 = J1939 extended */
    uint16_t dlc;          /* nominal payload bytes, up to 1785 */
    uint16_t signal_begin; /* contiguous slice of signals[] */
    uint16_t signal_count;
} j1939_message_t;

typedef struct j1939_spn_ref {
    uint32_t spn;
    uint16_t message_index;
    uint16_t signal_index; /* index into that message's slice, not the global array */
} j1939_spn_ref_t;

/* Sorted by signal name. Used to answer "which messages contain this signal?". */
typedef struct j1939_name_ref {
    uint32_t name_off;
    uint16_t message_index;
    uint16_t signal_index;
} j1939_name_ref_t;

typedef struct j1939_db {
    const char *strings;                 /* NULL when strings were stripped */
    const j1939_message_t *messages;     /* sorted by (extended, key) */
    uint16_t message_count;
    const j1939_signal_t *signals;
    uint16_t signal_count;
    const j1939_enum_t *enums;
    uint16_t enum_count;
    const j1939_spn_ref_t *spn_index;    /* sorted by spn; NULL if none */
    uint16_t spn_count;
    const j1939_name_ref_t *name_index;  /* sorted by name; NULL if no strings */
    uint16_t name_count;
} j1939_db_t;

/* Splits a CAN id. For a standard frame the J1939 fields are written as 0.
   `extended` is the caller's frame type; the EFF bit in can_id is ignored. */
void j1939_id_unpack(uint32_t can_id, int extended,
                     uint32_t *pgn, uint8_t *sa, uint8_t *da, uint8_t *priority);

/* One binary search. Priority and the EFF flag are not part of the key.
   An extended frame is found by PGN. Among rows that share a PGN, a
   source-address-specific row wins over the generic row (sa_mask == 0). */
const j1939_message_t *j1939_db_find(const j1939_db_t *db, uint32_t can_id, int extended);

/* First SPN index hit. `message` may be NULL. Returns NULL when spn is 0 or absent. */
const j1939_signal_t *j1939_signal_by_spn(const j1939_db_t *db, uint32_t spn,
                                          const j1939_message_t **message);

/* Linear scan of one message. Returns NULL when strings were stripped. */
const j1939_signal_t *j1939_signal_by_name(const j1939_db_t *db,
                                           const j1939_message_t *message,
                                           const char *name);

/* Writes up to `cap` message indices that contain a signal of this name.
   Returns the full match count, which may be greater than cap.
   `indices` may be NULL when cap is 0. */
uint16_t j1939_messages_by_name(const j1939_db_t *db, const char *name,
                                uint16_t *indices, uint16_t cap);

/* 0 on success. Integer signals wider than 64 bits fail. A signal that does
   not fit in `dlc` bytes fails. */
int j1939_signal_raw(const uint8_t *payload, uint16_t dlc,
                     const j1939_signal_t *sig, uint64_t *raw);

double j1939_signal_phys(const j1939_signal_t *sig, uint64_t raw);

/* Writes into `payload`, which must be large enough for the signal.
   Scale 0 fails. Byte strings wider than 64 bits fail. */
int j1939_signal_store(uint8_t *payload, const j1939_signal_t *sig, double phys);

/* Copies min(dst_size - 1, length/8) bytes and NUL-terminates.
   Requires J1939_SIG_BYTES and a byte-aligned start bit. */
int j1939_signal_bytes(const uint8_t *payload, uint16_t dlc,
                       const j1939_signal_t *sig, char *dst, uint16_t dst_size);

/* 1 when the signal should be decoded, 0 when a multiplex hides it, -1 on a bad read.
   A signal without J1939_SIG_MULTIPLEXED is always active. */
int j1939_signal_active(const j1939_db_t *db, const j1939_message_t *message,
                        const uint8_t *payload, uint16_t dlc,
                        const j1939_signal_t *sig);

#ifdef __cplusplus
}
#endif

#endif /* J1939_DB_H */
