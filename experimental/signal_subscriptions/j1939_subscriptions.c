#include "j1939_subscriptions.h"
#include <float.h>
_Static_assert(sizeof(double)==8 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024 && FLT_RADIX==2,
               "prototype requires binary64 double");

/* Small local text operations keep the core independent of libc string routines. */
static int text_ok(const j1939_db_metadata_t *m, uint32_t off) {
    uint32_t i;
    if (off >= m->strings_size) return 0;
    for (i = off; i < m->strings_size; ++i) if (!m->db->strings[i]) return 1;
    return 0;
}
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int ready(const j1939_sub_context_t *c) {
    return c && c->metadata && c->callback && (!c->capacity || c->slots);
}
static int metadata_ok(const j1939_db_metadata_t *m) {
    uint32_t i, j;
    const j1939_db_t *db;
    if (!m || m->version != J1939_DB_METADATA_VERSION || !(db = m->db) ||
        !db->strings || !m->strings_size || !db->messages || !db->signals ||
        !db->message_count || !db->signal_count || !m->signals || !m->sources ||
        !m->source_count || !m->message_definitions || !m->message_definition_count ||
        !m->signal_definitions || !m->signal_definition_count) return 0;
    for (i = 0; i < db->message_count; ++i) {
        const j1939_message_t *msg = &db->messages[i];
        if (!msg->dlc || msg->dlc > 8 || msg->extended > 1 || !msg->signal_count ||
            (uint32_t)msg->signal_begin + msg->signal_count > db->signal_count) return 0;
    }
    for (i = 0; i < db->signal_count; ++i) {
        const j1939_signal_t *s = &db->signals[i];
        const j1939_signal_metadata_t *sm = &m->signals[i];
        if (!s->length || s->length > 64 || (s->flags & J1939_SIG_BYTES) ||
            !text_ok(m, sm->unit_off) || !text_ok(m, sm->scale_off) ||
            !text_ok(m, sm->offset_off) || !text_ok(m, sm->minimum_off) ||
            !text_ok(m, sm->maximum_off)) return 0;
    }
    for (i = 0; i < m->source_count; ++i) if (!text_ok(m, m->sources[i].name_off)) return 0;
    for (i = 0; i < m->message_definition_count; ++i) {
        const j1939_message_definition_t *d = &m->message_definitions[i];
        const j1939_message_t *msg;
        if (d->message_index >= db->message_count || d->source_index >= m->source_count ||
            d->signal_begin > m->signal_definition_count ||
            d->signal_count > m->signal_definition_count - d->signal_begin ||
            !text_ok(m, d->name_off)) return 0;
        msg = &db->messages[d->message_index];
        if (d->signal_count != msg->signal_count ||
            j1939_db_find(db, d->dbc_id, msg->extended) != msg) return 0;
        for (j = 0; j < d->signal_count; ++j) {
            const j1939_signal_definition_t *sd = &m->signal_definitions[d->signal_begin+j];
            if (sd->signal_index != msg->signal_begin+j || !text_ok(m, sd->name_off)) return 0;
        }
    }
    return 1;
}
int j1939_sub_init(j1939_sub_context_t *c, const j1939_db_metadata_t *m,
                   j1939_sub_slot_t *slots, uint16_t capacity,
                   j1939_sub_callback_t callback, void *user) {
    uint16_t i;
    j1939_sub_context_t value = {0};
    if (!c || !callback || (capacity && !slots)) return J1939_SUB_INVALID;
    if (!metadata_ok(m)) return J1939_SUB_UNSUPPORTED;
    value.metadata=m; value.slots=slots; value.capacity=capacity;
    value.callback=callback; value.user=user; value.next_id=1;
    for (i=0; i<capacity; ++i) slots[i].id=0;
    *c=value;
    return J1939_SUB_OK;
}
int j1939_sub_select(const j1939_sub_context_t *c, const char *source,
                     const char *message, const char *signal, j1939_sub_ref_t *out) {
    uint32_t i, j;
    const j1939_db_metadata_t *m;
    if (!ready(c) || !source || !message || !signal || !out) return J1939_SUB_INVALID;
    if (c->busy) return J1939_SUB_BUSY;
    m=c->metadata;
    for (i=0; i<m->message_definition_count; ++i) {
        const j1939_message_definition_t *d=&m->message_definitions[i];
        if (!equal(source, m->db->strings+m->sources[d->source_index].name_off) ||
            !equal(message, m->db->strings+d->name_off)) continue;
        for (j=0; j<d->signal_count; ++j) {
            uint32_t index=d->signal_begin+j;
            if (equal(signal, m->db->strings+m->signal_definitions[index].name_off)) {
                j1939_sub_ref_t found;
                found.metadata=m; found.message_definition=i; found.signal_definition=index;
                *out=found; return J1939_SUB_OK;
            }
        }
    }
    return J1939_SUB_NOT_FOUND;
}
int j1939_subscribe(j1939_sub_context_t *c, j1939_sub_ref_t ref,
                    j1939_sub_source_policy_t policy, uint32_t tag, uint32_t *id) {
    uint16_t i;
    const j1939_message_definition_t *d;
    if (!ready(c) || !id) return J1939_SUB_INVALID;
    if (c->busy) return J1939_SUB_BUSY;
    if (ref.metadata!=c->metadata || ref.message_definition>=c->metadata->message_definition_count ||
        (policy!=J1939_SUB_DECLARED_SOURCE && policy!=J1939_SUB_ANY_ROUTED_SOURCE)) return J1939_SUB_INVALID;
    d=&c->metadata->message_definitions[ref.message_definition];
    if (ref.signal_definition<d->signal_begin || ref.signal_definition-d->signal_begin>=d->signal_count)
        return J1939_SUB_INVALID;
    if (!c->next_id) return J1939_SUB_ID_LIMIT;
    for (i=0; i<c->capacity; ++i) if (!c->slots[i].id) {
        j1939_sub_slot_t *s=&c->slots[i];
        s->selection=ref; s->message_index=d->message_index;
        s->signal_index=c->metadata->signal_definitions[ref.signal_definition].signal_index;
        s->source=policy==J1939_SUB_DECLARED_SOURCE ? (uint16_t)(d->dbc_id & 255u) : 256;
        s->tag=tag; s->id=c->next_id++;
        *id=s->id; return J1939_SUB_OK;
    }
    return J1939_SUB_FULL;
}
int j1939_unsubscribe(j1939_sub_context_t *c, uint32_t id) {
    uint16_t i;
    if (!ready(c) || !id) return J1939_SUB_INVALID;
    if (c->busy) return J1939_SUB_BUSY;
    for (i=0; i<c->capacity; ++i) if (c->slots[i].id==id) {
        c->slots[i].id=0; return J1939_SUB_OK;
    }
    return J1939_SUB_NOT_FOUND;
}
static int matches(const j1939_sub_slot_t *s, uint16_t message, int extended, uint8_t source) {
    return s->id && s->message_index==message &&
        (!extended || s->source==256 || s->source==source);
}
int j1939_sub_process(j1939_sub_context_t *c, const j1939_sub_frame_t *f, uint16_t *out_count) {
    const j1939_message_t *msg;
    const j1939_db_t *db;
    uint16_t i, index, delivered=0;
    uint32_t pgn;
    uint8_t sa;
    if (!ready(c) || !f) return J1939_SUB_INVALID;
    if (c->busy) return J1939_SUB_BUSY;
    if (f->extended>1 || f->can_id>(f->extended ? UINT32_C(0x1fffffff) : UINT32_C(0x7ff)) ||
        (f->size && !f->data)) return J1939_SUB_INVALID;
    if (f->size>8 || (f->extended && (f->can_id & UINT32_C(0x02000000)))) return J1939_SUB_UNSUPPORTED;
    db=c->metadata->db;
    msg=j1939_db_find(db, f->can_id, f->extended);
    if (!msg) { if (out_count) *out_count=0; return J1939_SUB_OK; }
    if (f->size<msg->dlc) return J1939_SUB_INVALID;
    index=(uint16_t)(msg-db->messages);
    j1939_id_unpack(f->can_id, f->extended, &pgn, &sa, NULL, NULL);
    /* Preflight every matching subscription before any callback. The caller
     * must keep frame/payload/metadata unchanged through both passes. */
    for (i=0; i<c->capacity; ++i) if (matches(&c->slots[i], index, f->extended, sa)) {
        const j1939_signal_t *s=&db->signals[c->slots[i].signal_index];
        uint64_t raw;
        int active=j1939_signal_active(db,msg,f->data,f->size,s);
        if (active<0 || (active && j1939_signal_raw(f->data,f->size,s,&raw))) return J1939_SUB_UNSUPPORTED;
    }
    c->busy=1;
    for (i=0; i<c->capacity; ++i) if (matches(&c->slots[i], index, f->extended, sa)) {
        const j1939_sub_slot_t *slot=&c->slots[i];
        const j1939_signal_t *s=&db->signals[slot->signal_index];
        j1939_sub_update_t u={0};
        if (j1939_signal_active(db,msg,f->data,f->size,s)!=1) continue;
        (void)j1939_signal_raw(f->data,f->size,s,&u.raw); /* checked in preflight */
        u.subscription_id=slot->id; u.tag=slot->tag; u.selection=slot->selection;
        u.can_id=f->can_id; u.pgn=pgn; u.source_address=sa;
        u.signal=s; u.interpretation=&c->metadata->signals[slot->signal_index];
        u.is_signed=(s->flags & J1939_SIG_SIGNED)!=0;
        if (u.is_signed) {
            uint64_t mask=s->length==64 ? UINT64_MAX : (UINT64_C(1)<<s->length)-1;
            u.signed_raw=(u.raw & (UINT64_C(1)<<(s->length-1))) ?
                -1-(int64_t)(~u.raw & mask) : (int64_t)u.raw;
        }
        u.engineering=j1939_signal_phys(s,u.raw);
        u.engineering_finite=u.engineering<=DBL_MAX && u.engineering>=-DBL_MAX;
        u.unit=j1939_signal_unit(c->metadata,slot->signal_index);
        u.quality=J1939_SUB_QUALITY_UNKNOWN;
        if (f->timestamp) { u.timestamp=*f->timestamp; u.has_timestamp=1; }
        c->callback(c->user,&u);
        ++delivered;
    }
    c->busy=0;
    if (out_count) *out_count=delivered;
    return J1939_SUB_OK;
}
