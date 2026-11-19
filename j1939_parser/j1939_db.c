#include "j1939_db.h"

#include <string.h>

static uint64_t bit_mask(unsigned length) {
    if (length >= 64) {
        return ~(uint64_t)0;
    }
    return ((uint64_t)1 << length) - 1u;
}

static uint32_t message_key(const j1939_message_t *m) {
    if (m->extended) {
        return m->pgn;
    }
    return m->can_id & 0x7FFu;
}

/* <0 when (ext, key) is before m, >0 when it is after, 0 when equal. */
static int cmp_message_key(uint8_t ext, uint32_t key, const j1939_message_t *m) {
    if (ext != m->extended) {
        return ext < m->extended ? -1 : 1;
    }
    uint32_t mk = message_key(m);
    if (key < mk) {
        return -1;
    }
    if (key > mk) {
        return 1;
    }
    return 0;
}

static uint16_t lower_bound_message(const j1939_db_t *db, uint8_t ext, uint32_t key) {
    uint16_t lo = 0;
    uint16_t hi = db->message_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        if (cmp_message_key(ext, key, &db->messages[mid]) > 0) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    return lo;
}

static int signal_fits(const j1939_signal_t *sig, uint16_t dlc) {
    uint32_t last;
    if (sig->length == 0) {
        return 0;
    }
    if (sig->flags & J1939_SIG_BYTES) {
        if ((sig->start_bit % 8u) != 0) {
            return 0;
        }
        last = (uint32_t)(sig->start_bit / 8u) + (uint32_t)(sig->length / 8u);
        if ((sig->length % 8u) != 0) {
            last += 1u;
        }
        return last <= dlc;
    }
    if (sig->flags & J1939_SIG_LITTLE_ENDIAN) {
        last = ((uint32_t)sig->start_bit + sig->length - 1u) / 8u + 1u;
        return last <= dlc;
    }
    {
        uint32_t msb_index = sig->start_bit / 8u;
        uint32_t msb_len = sig->start_bit % 8u + 1u;
        uint32_t extra = 0;
        if (msb_len > sig->length) {
            msb_len = sig->length;
        }
        if (sig->length > msb_len) {
            extra = (sig->length - msb_len + 7u) / 8u;
        }
        last = msb_index + extra + 1u;
        return last <= dlc;
    }
}

/* Same bit numbering as J1939_frame::read_bits. `length` is 1..64 and the
   bytes the signal touches are inside `dlc`. */
static uint64_t read_bits(const uint8_t *payload, uint16_t start_bit, uint8_t length, int little_endian) {
    uint64_t target = 0;
    if (!little_endian) {
        unsigned msb_index = start_bit / 8u;
        unsigned msb_len = start_bit % 8u + 1u;
        if (msb_len > length) {
            msb_len = length;
        }
        if (msb_len >= length) {
            target |= (uint64_t)payload[msb_index] >> ((start_bit % 8u) - length + 1u);
        } else {
            unsigned lsb_index = msb_index + (length - msb_len + 7u) / 8u;
            unsigned lsb_len = (length - msb_len - 1u) % 8u + 1u;
            unsigned lsb_bit_offset = 8u - lsb_len;
            unsigned loaded = 1;
            unsigned index;
            target |= ((uint64_t)payload[lsb_index] >> lsb_bit_offset) & bit_mask(lsb_len);
            for (index = msb_index; index < lsb_index; index++) {
                int shift = (int)length - (int)msb_len - (int)((loaded - 1u) * 8u);
                if (index == msb_index) {
                    target |= ((uint64_t)payload[index] & bit_mask(msb_len)) << shift;
                } else {
                    target |= (uint64_t)payload[index] << shift;
                }
                loaded++;
            }
        }
    } else {
        unsigned lsb_index = start_bit / 8u;
        unsigned lsb_bit_offset = start_bit % 8u;
        unsigned msb_index = (start_bit + length - 1u) / 8u;
        unsigned loaded = 1;
        unsigned index;
        target |= (uint64_t)payload[lsb_index] >> lsb_bit_offset;
        for (index = lsb_index + 1u; index <= msb_index; index++) {
            unsigned shift = (loaded * 8u) - lsb_bit_offset;
            target |= (uint64_t)payload[index] << shift;
            loaded++;
        }
    }
    return target & bit_mask(length);
}

static void write_bits(uint8_t *payload, uint64_t value, uint16_t start_bit, uint8_t length, int little_endian) {
    value &= bit_mask(length);
    if (!little_endian) {
        unsigned msb_index = start_bit / 8u;
        unsigned msb_len = start_bit % 8u + 1u;
        unsigned lsb_index;
        unsigned lsb_len;
        unsigned lsb_bit_offset;
        if (msb_len > length) {
            msb_len = length;
        }
        lsb_index = msb_index + (length - msb_len + 7u) / 8u;
        lsb_len = (length - msb_len - 1u) % 8u + 1u;
        lsb_bit_offset = 8u - lsb_len;
        if (msb_index == lsb_index) {
            unsigned shift = (start_bit % 8u) + 1u - length;
            unsigned mask = ((1u << length) - 1u) << shift;
            payload[msb_index] = (uint8_t)((payload[msb_index] & (uint8_t)~mask) | ((value << shift) & mask));
        } else {
            unsigned lsb_mask = 0xFFu << lsb_bit_offset;
            unsigned loaded = 1;
            int index;
            payload[lsb_index] = (uint8_t)((payload[lsb_index] & (uint8_t)~lsb_mask) |
                                           ((value << lsb_bit_offset) & lsb_mask));
            for (index = (int)lsb_index - 1; index >= (int)msb_index; index--) {
                unsigned mask = 0xFFu;
                int remaining = (int)length - (int)((loaded + 1u) * 8u) + (int)lsb_bit_offset;
                unsigned right_shift;
                if (remaining < 0) {
                    mask = 0xFFu >> (unsigned)(-remaining);
                }
                right_shift = 8u * (loaded - 1u) + lsb_len;
                payload[index] = (uint8_t)((payload[index] & (uint8_t)~mask) | ((value >> right_shift) & mask));
                loaded++;
            }
        }
    } else {
        unsigned lsb_index = start_bit / 8u;
        unsigned lsb_bit_offset = start_bit % 8u;
        unsigned take = 8u - lsb_bit_offset;
        unsigned mask;
        unsigned msb_index;
        unsigned loaded = 1;
        unsigned index;
        if (take > length) {
            take = length;
        }
        mask = ((1u << take) - 1u) << lsb_bit_offset;
        payload[lsb_index] = (uint8_t)((payload[lsb_index] & (uint8_t)~mask) |
                                       ((value << lsb_bit_offset) & mask));
        msb_index = (start_bit + length - 1u) / 8u;
        for (index = lsb_index + 1u; index <= msb_index; index++) {
            int remaining = (int)length - (int)(8u - lsb_bit_offset) - (int)(loaded * 8u);
            unsigned right_shift;
            if (remaining < 0) {
                mask = 0xFFu >> (unsigned)(-remaining);
            } else {
                mask = 0xFFu;
            }
            right_shift = loaded * 8u - lsb_bit_offset;
            payload[index] = (uint8_t)((payload[index] & (uint8_t)~mask) | ((value >> right_shift) & mask));
            loaded++;
        }
    }
}

static int64_t sign_extend(uint64_t raw, unsigned length) {
    uint64_t sign = (uint64_t)1 << (length - 1u);
    return (int64_t)((raw ^ sign) - sign);
}

void j1939_id_unpack(uint32_t can_id, int extended,
                     uint32_t *pgn, uint8_t *sa, uint8_t *da, uint8_t *priority) {
    uint32_t id = can_id & 0x1FFFFFFFu;
    uint32_t pf;
    uint32_t ps;
    uint32_t dp;
    if (!extended) {
        if (pgn) {
            *pgn = 0;
        }
        if (sa) {
            *sa = 0;
        }
        if (da) {
            *da = 0;
        }
        if (priority) {
            *priority = 0;
        }
        return;
    }
    dp = (id >> 24) & 1u;
    pf = (id >> 16) & 0xFFu;
    ps = (id >> 8) & 0xFFu;
    if (pgn) {
        if (pf < 240u) {
            *pgn = (dp << 16) | (pf << 8);
        } else {
            *pgn = (dp << 16) | (pf << 8) | ps;
        }
    }
    if (sa) {
        *sa = (uint8_t)(id & 0xFFu);
    }
    if (da) {
        *da = pf < 240u ? (uint8_t)ps : 0xFFu;
    }
    if (priority) {
        *priority = (uint8_t)((id >> 26) & 7u);
    }
}

const j1939_message_t *j1939_db_find(const j1939_db_t *db, uint32_t can_id, int extended) {
    uint32_t pgn = 0;
    uint8_t sa = 0;
    uint32_t key;
    uint16_t i;
    const j1939_message_t *generic = NULL;
    const j1939_message_t *specific = NULL;
    if (db == NULL || db->messages == NULL || db->message_count == 0) {
        return NULL;
    }
    if (extended && (can_id & 0x02000000u) != 0) {
        return NULL;
    }
    j1939_id_unpack(can_id, extended, &pgn, &sa, NULL, NULL);
    key = extended ? pgn : (can_id & 0x7FFu);
    i = lower_bound_message(db, extended ? 1u : 0u, key);
    for (; i < db->message_count; i++) {
        const j1939_message_t *m = &db->messages[i];
        if (cmp_message_key(extended ? 1u : 0u, key, m) != 0) {
            break;
        }
        if (m->sa_mask == 0) {
            if (generic == NULL) {
                generic = m;
            }
        } else if (m->sa == sa && specific == NULL) {
            specific = m;
        }
    }
    return specific != NULL ? specific : generic;
}

const j1939_signal_t *j1939_signal_by_spn(const j1939_db_t *db, uint32_t spn,
                                          const j1939_message_t **message) {
    uint16_t lo;
    uint16_t hi;
    const j1939_spn_ref_t *hit;
    if (message) {
        *message = NULL;
    }
    if (db == NULL || spn == 0 || db->spn_index == NULL || db->spn_count == 0) {
        return NULL;
    }
    lo = 0;
    hi = db->spn_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        if (db->spn_index[mid].spn < spn) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    if (lo >= db->spn_count || db->spn_index[lo].spn != spn) {
        return NULL;
    }
    hit = &db->spn_index[lo];
    if (hit->message_index >= db->message_count) {
        return NULL;
    }
    if (message) {
        *message = &db->messages[hit->message_index];
    }
    {
        const j1939_message_t *m = &db->messages[hit->message_index];
        if (db->signals == NULL || hit->signal_index >= m->signal_count) {
            return NULL;
        }
        return &db->signals[m->signal_begin + hit->signal_index];
    }
}

static const char *signal_name(const j1939_db_t *db, const j1939_signal_t *sig) {
    if (db->strings == NULL || sig->name_off == J1939_NO_NAME) {
        return NULL;
    }
    return db->strings + sig->name_off;
}

const j1939_signal_t *j1939_signal_by_name(const j1939_db_t *db,
                                           const j1939_message_t *message,
                                           const char *name) {
    uint16_t i;
    if (db == NULL || message == NULL || name == NULL || db->signals == NULL || db->strings == NULL) {
        return NULL;
    }
    if ((uint32_t)message->signal_begin + message->signal_count > db->signal_count) {
        return NULL;
    }
    for (i = 0; i < message->signal_count; i++) {
        const j1939_signal_t *sig = &db->signals[message->signal_begin + i];
        const char *got = signal_name(db, sig);
        if (got != NULL && strcmp(got, name) == 0) {
            return sig;
        }
    }
    return NULL;
}

uint16_t j1939_messages_by_name(const j1939_db_t *db, const char *name,
                                uint16_t *indices, uint16_t cap) {
    uint16_t lo;
    uint16_t hi;
    uint16_t count = 0;
    uint16_t filled = 0;
    if (db == NULL || name == NULL || db->strings == NULL || db->name_index == NULL || db->name_count == 0) {
        return 0;
    }
    lo = 0;
    hi = db->name_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        const char *got = db->strings + db->name_index[mid].name_off;
        int cmp = strcmp(got, name);
        if (cmp < 0) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    for (; lo < db->name_count; lo++) {
        const char *got = db->strings + db->name_index[lo].name_off;
        if (strcmp(got, name) != 0) {
            break;
        }
        if (indices != NULL && filled < cap) {
            indices[filled] = db->name_index[lo].message_index;
            filled++;
        }
        count++;
    }
    return count;
}

int j1939_signal_raw(const uint8_t *payload, uint16_t dlc,
                     const j1939_signal_t *sig, uint64_t *raw) {
    if (payload == NULL || sig == NULL || raw == NULL) {
        return -1;
    }
    if (sig->length == 0 || sig->length > 64) {
        return -1;
    }
    if (!signal_fits(sig, dlc)) {
        return -1;
    }
    *raw = read_bits(payload, sig->start_bit, (uint8_t)sig->length,
                     (sig->flags & J1939_SIG_LITTLE_ENDIAN) ? 1 : 0);
    return 0;
}

double j1939_signal_phys(const j1939_signal_t *sig, uint64_t raw) {
    double value;
    if (sig == NULL || sig->length == 0 || sig->length > 64) {
        return 0.0;
    }
    raw &= bit_mask(sig->length);
    if (sig->flags & J1939_SIG_SIGNED) {
        value = (double)sign_extend(raw, sig->length);
    } else {
        value = (double)raw;
    }
    if (sig->scale == 1.0 && sig->offset == 0.0) {
        return value;
    }
    return value * sig->scale + sig->offset;
}

int j1939_signal_store(uint8_t *payload, const j1939_signal_t *sig, double phys) {
    double scaled;
    int64_t as_int;
    if (payload == NULL || sig == NULL) {
        return -1;
    }
    if (sig->length == 0 || sig->length > 64 || sig->scale == 0.0) {
        return -1;
    }
    if (sig->scale == 1.0 && sig->offset == 0.0) {
        scaled = phys;
    } else if (phys < 0.0) {
        scaled = (phys - sig->offset) / sig->scale - 0.5;
    } else {
        scaled = (phys - sig->offset) / sig->scale + 0.5;
    }
    as_int = (int64_t)scaled;
    write_bits(payload, (uint64_t)as_int, sig->start_bit, (uint8_t)sig->length,
               (sig->flags & J1939_SIG_LITTLE_ENDIAN) ? 1 : 0);
    return 0;
}

int j1939_signal_bytes(const uint8_t *payload, uint16_t dlc,
                       const j1939_signal_t *sig, char *dst, uint16_t dst_size) {
    uint16_t nbytes;
    uint16_t copy;
    uint16_t start;
    if (payload == NULL || sig == NULL || dst == NULL || dst_size == 0) {
        return -1;
    }
    if ((sig->flags & J1939_SIG_BYTES) == 0 || (sig->start_bit % 8u) != 0) {
        return -1;
    }
    if (!signal_fits(sig, dlc)) {
        return -1;
    }
    nbytes = (uint16_t)(sig->length / 8u);
    start = (uint16_t)(sig->start_bit / 8u);
    copy = nbytes;
    if (copy > (uint16_t)(dst_size - 1u)) {
        copy = (uint16_t)(dst_size - 1u);
    }
    if (copy > 0) {
        memcpy(dst, payload + start, copy);
    }
    dst[copy] = '\0';
    return 0;
}

int j1939_signal_active(const j1939_db_t *db, const j1939_message_t *message,
                        const uint8_t *payload, uint16_t dlc,
                        const j1939_signal_t *sig) {
    uint16_t i;
    const j1939_signal_t *sw = NULL;
    uint64_t raw = 0;
    if (db == NULL || message == NULL || sig == NULL) {
        return -1;
    }
    if ((sig->flags & J1939_SIG_MULTIPLEXED) == 0) {
        return 1;
    }
    if (db->signals == NULL) {
        return -1;
    }
    for (i = 0; i < message->signal_count; i++) {
        const j1939_signal_t *s = &db->signals[message->signal_begin + i];
        if ((s->flags & J1939_SIG_MULTIPLEXOR) != 0 && (s->flags & J1939_SIG_MULTIPLEXED) == 0) {
            sw = s;
            break;
        }
    }
    if (sw == NULL) {
        return 0;
    }
    if (j1939_signal_raw(payload, dlc, sw, &raw) != 0) {
        return -1;
    }
    return raw == sig->mux_value ? 1 : 0;
}
