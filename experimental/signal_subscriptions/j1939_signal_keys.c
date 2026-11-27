#include "j1939_signal_keys.h"

static uint32_t mask(uint8_t bits) { return UINT32_MAX>>(32u-bits); }
int j1939_key_layout_make(uint32_t d,uint32_t s,uint32_t id,j1939_key_layout_t *out) {
    j1939_key_layout_t value;
    if(!out || !d || !s || !id || d>32 || s>32 || id>32 || d+s+id!=32)return J1939_SUB_INVALID;
    value.domain_bits=(uint8_t)d;value.source_bits=(uint8_t)s;value.signal_bits=(uint8_t)id;*out=value;return 0;
}
int j1939_key_layout_valid(j1939_key_layout_t l) {
    return l.domain_bits && l.source_bits && l.signal_bits &&
        l.domain_bits<=32 && l.source_bits<=32 && l.signal_bits<=32 &&
        (unsigned)l.domain_bits+l.source_bits+l.signal_bits==32;
}
int j1939_key_pack(j1939_key_layout_t l,uint32_t d,uint32_t s,uint32_t id,uint32_t *out) {
    if (!out || !j1939_key_layout_valid(l) || d>mask(l.domain_bits) || s>mask(l.source_bits) || id>mask(l.signal_bits))
        return J1939_SUB_INVALID;
    *out=(d<<(l.source_bits+l.signal_bits))|(s<<l.signal_bits)|id;
    return 0;
}
int j1939_key_unpack(j1939_key_layout_t l,uint32_t key,uint32_t *d,uint32_t *s,uint32_t *id) {
    if (!d || !s || !id || !j1939_key_layout_valid(l)) return J1939_SUB_INVALID;
    *d=key>>(l.source_bits+l.signal_bits);*s=(key>>l.signal_bits)&mask(l.source_bits);*id=key&mask(l.signal_bits);
    return 0;
}
static uint64_t number(uint64_t h,uint64_t value) {
    unsigned i;for(i=0;i<8;++i){h^=(uint8_t)value;h*=UINT64_C(1099511628211);value>>=8;}return h;
}
static uint64_t floating(uint64_t h,double value) {
    union { double d;uint64_t bits; } u;u.d=value;return number(h,u.bits);
}
uint64_t j1939_key_metadata_hash(const j1939_db_metadata_t *m) {
    const j1939_db_t *db=m->db;uint32_t i;uint64_t h=UINT64_C(14695981039346656037);
    h=number(h,m->version);h=number(h,m->strings_size);
    for(i=0;i<m->strings_size;++i){h^=(uint8_t)db->strings[i];h*=UINT64_C(1099511628211);}
    h=number(h,db->message_count);h=number(h,db->signal_count);h=number(h,db->enum_count);
    for(i=0;i<db->message_count;++i){const j1939_message_t *v=&db->messages[i];
        h=number(h,v->name_off);h=number(h,v->pgn);h=number(h,v->can_id);h=number(h,v->sa_mask);
        h=number(h,v->sa);h=number(h,v->extended);h=number(h,v->dlc);h=number(h,v->signal_begin);h=number(h,v->signal_count);}
    for(i=0;i<db->signal_count;++i){const j1939_signal_t *v=&db->signals[i];const j1939_signal_metadata_t *s=&m->signals[i];
        h=number(h,v->name_off);h=number(h,v->spn);h=number(h,v->start_bit);h=number(h,v->length);
        h=number(h,v->enum_begin);h=number(h,v->enum_count);h=number(h,v->mux_value);h=number(h,v->flags);
        h=floating(h,v->scale);h=floating(h,v->offset);h=floating(h,v->min_value);h=floating(h,v->max_value);
        h=number(h,s->unit_off);h=number(h,s->scale_off);h=number(h,s->offset_off);h=number(h,s->minimum_off);h=number(h,s->maximum_off);}
    for(i=0;i<db->enum_count;++i){h=number(h,db->enums[i].value);h=number(h,db->enums[i].name_off);}
    h=number(h,db->spn_count);h=number(h,db->name_count);
    for(i=0;i<db->spn_count;++i){h=number(h,db->spn_index[i].spn);h=number(h,db->spn_index[i].message_index);h=number(h,db->spn_index[i].signal_index);}
    for(i=0;i<db->name_count;++i){h=number(h,db->name_index[i].name_off);h=number(h,db->name_index[i].message_index);h=number(h,db->name_index[i].signal_index);}
    h=number(h,m->source_count);h=number(h,m->message_definition_count);h=number(h,m->signal_definition_count);
    for(i=0;i<m->source_count;++i){const j1939_source_metadata_t *s=&m->sources[i];h=number(h,s->name_off);h=number(h,s->version_off);h=number(h,s->byte_length);h=number(h,s->fnv1a64);}
    for(i=0;i<m->message_definition_count;++i){const j1939_message_definition_t *d=&m->message_definitions[i];
        h=number(h,d->dbc_id);h=number(h,d->name_off);h=number(h,d->line);h=number(h,d->spn);h=number(h,d->signal_begin);
        h=number(h,d->source_index);h=number(h,d->message_index);h=number(h,d->signal_count);h=number(h,d->flags);}
    for(i=0;i<m->signal_definition_count;++i){const j1939_signal_definition_t *d=&m->signal_definitions[i];h=number(h,d->name_off);h=number(h,d->line);h=number(h,d->spn);h=number(h,d->signal_index);h=number(h,d->flags);}
    return h;
}
int j1939_key_identity_equal(const j1939_key_identity_t *a,const j1939_key_identity_t *b) {
    unsigned i;if(!a || !b)return 0;
    for(i=0;i<32;++i)if(a->catalog_sha256[i]!=b->catalog_sha256[i])return 0;
    return a->version==b->version && a->mapping_hash==b->mapping_hash && a->source_namespace==b->source_namespace &&
        a->source_mode==b->source_mode && a->layout.domain_bits==b->layout.domain_bits &&
        a->layout.source_bits==b->layout.source_bits && a->layout.signal_bits==b->layout.signal_bits;
}
static int source_known(const j1939_key_context_t *c,j1939_key_filter_t f) {
    uint32_t i;if(c->identity.source_mode==J1939_KEY_SOURCE_ADDRESS)return f.any_source || f.source<=255;
    for(i=0;i<c->mapping_count;++i)if((f.any_domain || f.domain==c->mapping[i].domain) &&
        (f.any_source || f.source==c->mapping[i].logical_ecu))return 1;
    return 0;
}
static int address_routes(const j1939_key_context_t *c,uint32_t id,uint8_t sa) {
    const j1939_key_entry_t *e=&c->catalog->entries[id];
    const j1939_message_definition_t *d=&c->catalog->metadata->message_definitions[e->message_definition];
    const j1939_message_t *m=&c->catalog->metadata->db->messages[d->message_index];
    return m->extended && j1939_db_find(c->catalog->metadata->db,(d->dbc_id & UINT32_C(0x1fffff00))|sa,1)==m;
}
static int can_route(const j1939_key_context_t *c,uint32_t id,j1939_key_filter_t f) {
    uint32_t i;
    if(c->identity.source_mode==J1939_KEY_SOURCE_ADDRESS) {
        if(!f.any_source)return address_routes(c,id,(uint8_t)f.source);
        for(i=0;i<256;++i)if(address_routes(c,id,(uint8_t)i))return 1;
    } else for(i=0;i<c->mapping_count;++i)
        if((f.any_domain || f.domain==c->mapping[i].domain) && (f.any_source || f.source==c->mapping[i].logical_ecu) &&
           address_routes(c,id,(uint8_t)c->mapping[i].address))return 1;
    return 0;
}
static void deliver(void *user,const j1939_sub_update_t *u) {
    j1939_key_context_t *c=(j1939_key_context_t *)user;const j1939_key_slot_t *s=&c->key_slots[u->tag];
    j1939_key_update_t out;
    if((!s->filter.any_domain && s->filter.domain!=c->current_domain) ||
       (!s->filter.any_source && s->filter.source!=c->current_source))return;
    out.value=*u;out.value.tag=s->tag;out.domain=c->current_domain;out.source_identity=c->current_source;
    out.signal_id=s->signal_id;
    (void)j1939_key_pack(c->identity.layout,out.domain,out.source_identity,out.signal_id,&out.packed_key);
    ++c->delivered;c->current_update=&out;c->callback(c->user,&out);c->current_update=NULL;
}
int j1939_keys_init(j1939_key_context_t *c,const j1939_key_catalog_t *cat,const uint8_t expected[32],
    j1939_key_layout_t l,j1939_key_source_mode_t mode,uint64_t ns,
    const j1939_key_mapping_t *mapping,uint32_t n,j1939_sub_slot_t *slots,j1939_key_slot_t *filters,
    uint16_t capacity,j1939_key_callback_t callback,void *user) {
    j1939_key_context_t value={0};uint32_t i,j;int status;uint64_t hash=UINT64_C(14695981039346656037);
    if(!c || !cat || !expected || !callback || !j1939_key_layout_valid(l) || !cat->entries || !cat->count ||
       cat->version!=J1939_KEY_VERSION || (capacity && (!slots || !filters)) ||
       (mode!=J1939_KEY_SOURCE_ADDRESS && mode!=J1939_KEY_LOGICAL_ECU) ||
       (mode==J1939_KEY_SOURCE_ADDRESS && (l.source_bits<8 || ns || n)) ||
       (mode==J1939_KEY_LOGICAL_ECU && (!ns || !n || !mapping)))return J1939_SUB_INVALID;
    for(i=0;i<32;++i)if(expected[i]!=cat->sha256[i])return J1939_KEY_MISMATCH;
    status=j1939_sub_init(&value.base,cat->metadata,NULL,0,deliver,c);if(status)return status;
    if(cat->count-1>mask(l.signal_bits))return J1939_SUB_ID_LIMIT;
    if(cat->metadata_hash!=j1939_key_metadata_hash(cat->metadata))return J1939_KEY_MISMATCH;
    if(cat->count!=cat->metadata->signal_definition_count)return J1939_KEY_MISMATCH;
    hash=number(hash,cat->count);
    for(i=0;i<cat->count;++i){const j1939_key_entry_t *e=&cat->entries[i];const j1939_message_definition_t *d;
        if(e->message_definition>=cat->metadata->message_definition_count)return J1939_KEY_MISMATCH;
        hash=number(hash,e->message_definition);hash=number(hash,e->signal_definition);
        d=&cat->metadata->message_definitions[e->message_definition];
        if(e->signal_definition<d->signal_begin || e->signal_definition-d->signal_begin>=d->signal_count)return J1939_KEY_MISMATCH;
        for(j=0;j<i;++j)if(cat->entries[j].signal_definition==e->signal_definition)return J1939_KEY_AMBIGUOUS;
    }
    if(hash!=cat->entry_hash)return J1939_KEY_MISMATCH;
    hash=UINT64_C(14695981039346656037);
    for(i=0;i<n;++i){const j1939_key_mapping_t *m=&mapping[i];
        if(m->address>255 || m->domain>mask(l.domain_bits) || m->logical_ecu>mask(l.source_bits))return J1939_SUB_INVALID;
        if(i && (mapping[i-1].domain>m->domain || (mapping[i-1].domain==m->domain && mapping[i-1].address>=m->address)))
            return mapping[i-1].domain==m->domain && mapping[i-1].address==m->address ? J1939_KEY_AMBIGUOUS : J1939_SUB_INVALID;
        hash=number(hash,m->domain);hash=number(hash,m->address);hash=number(hash,m->logical_ecu);
    }
    value.catalog=cat;value.mapping=mapping;value.mapping_count=n;value.key_slots=filters;value.callback=callback;value.user=user;
    for(i=0;i<32;++i)value.identity.catalog_sha256[i]=cat->sha256[i];
    value.identity.mapping_hash=hash;value.identity.source_namespace=ns;value.identity.version=J1939_KEY_VERSION;
    value.identity.layout=l;value.identity.source_mode=(uint8_t)mode;
    status=j1939_sub_init(&value.base,cat->metadata,slots,capacity,deliver,c);if(status)return status;
    *c=value;return 0;
}
int j1939_keys_subscribe(j1939_key_context_t *c,const j1939_key_identity_t *expected,
    uint32_t id,j1939_key_filter_t f,uint32_t tag,uint32_t *token) {
    uint16_t i;const j1939_key_entry_t *e;j1939_sub_ref_t ref;int result;
    if(!c || !c->catalog || !token)return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    if(!j1939_key_identity_equal(expected,&c->identity))return J1939_KEY_MISMATCH;
    if(id>=c->catalog->count || f.any_domain>1 || f.any_source>1 ||
       (f.any_domain ? f.domain!=0 : f.domain>mask(c->identity.layout.domain_bits)) ||
       (f.any_source ? f.source!=0 : f.source>mask(c->identity.layout.source_bits)))return J1939_SUB_INVALID;
    if(c->identity.source_mode==J1939_KEY_SOURCE_ADDRESS && !f.any_source && f.source>255)return J1939_SUB_INVALID;
    if(!source_known(c,f))return J1939_KEY_MAPPING_MISSING;
    e=&c->catalog->entries[id];ref.metadata=c->catalog->metadata;ref.message_definition=e->message_definition;ref.signal_definition=e->signal_definition;
    if(!ref.metadata->db->messages[ref.metadata->message_definitions[e->message_definition].message_index].extended)return J1939_SUB_UNSUPPORTED;
    if(!can_route(c,id,f))return J1939_SUB_INVALID;
    for(i=0;i<c->base.capacity;++i)if(!c->base.slots[i].id){
        result=j1939_subscribe(&c->base,ref,J1939_SUB_ANY_ROUTED_SOURCE,i,token);if(result)return result;
        c->key_slots[i].filter=f;c->key_slots[i].signal_id=id;c->key_slots[i].tag=tag;return 0;
    }
    return J1939_SUB_FULL;
}
int j1939_keys_subscribe_key(j1939_key_context_t *c,const j1939_key_identity_t *expected,uint32_t key,uint32_t tag,uint32_t *token) {
    j1939_sub_ref_t ref;j1939_key_filter_t f={0};uint32_t id;int result;
    result=j1939_keys_decode(c,expected,key,&ref);if(result)return result;
    result=j1939_key_unpack(c->identity.layout,key,&f.domain,&f.source,&id);
    if(result)return result;
    return j1939_keys_subscribe(c,expected,id,f,tag,token);
}
int j1939_keys_unsubscribe(j1939_key_context_t *c,uint32_t token) {
    if(!c || !c->catalog)return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    return j1939_unsubscribe(&c->base,token);
}
int j1939_keys_process(j1939_key_context_t *c,uint32_t domain,const j1939_sub_frame_t *f,uint16_t *count) {
    uint32_t i,source;int found=0,result;
    if(!c || !c->catalog || !f)return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    if(domain>mask(c->identity.layout.domain_bits) || f->extended>1 ||
       f->can_id>(f->extended?UINT32_C(0x1fffffff):UINT32_C(0x7ff)) || (f->size && !f->data))return J1939_SUB_INVALID;
    if(!f->extended || f->size>8 || (f->can_id & UINT32_C(0x02000000)))return J1939_SUB_UNSUPPORTED;
    source=f->can_id & 255u;
    if(c->identity.source_mode==J1939_KEY_LOGICAL_ECU){
        for(i=0;i<c->mapping_count;++i)if(c->mapping[i].domain==domain && c->mapping[i].address==source){source=c->mapping[i].logical_ecu;found=1;break;}
        if(!found)return J1939_KEY_MAPPING_MISSING;
    }
    c->current_domain=domain;c->current_source=source;c->delivered=0;c->busy=1;
    result=j1939_sub_process(&c->base,f,NULL);c->busy=0;
    if(!result && count)*count=c->delivered;
    return result;
}
int j1939_keys_decode(const j1939_key_context_t *c,const j1939_key_identity_t *expected,uint32_t key,j1939_sub_ref_t *out) {
    uint32_t d,s,id;j1939_sub_ref_t ref;j1939_key_filter_t f={0};
    if(!c || !c->catalog || !out)return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    if(!j1939_key_identity_equal(expected,&c->identity))return J1939_KEY_MISMATCH;
    if(j1939_key_unpack(c->identity.layout,key,&d,&s,&id))return J1939_SUB_INVALID;
    if(id>=c->catalog->count)return J1939_SUB_NOT_FOUND;
    f.domain=d;f.source=s;
    if(c->identity.source_mode==J1939_KEY_SOURCE_ADDRESS && s>255)return J1939_SUB_INVALID;
    if(!source_known(c,f))return J1939_KEY_MAPPING_MISSING;
    if(!can_route(c,id,f))return J1939_SUB_INVALID;
    ref.metadata=c->catalog->metadata;ref.message_definition=c->catalog->entries[id].message_definition;
    ref.signal_definition=c->catalog->entries[id].signal_definition;*out=ref;return 0;
}
uint32_t j1939_keys_by_spn(const j1939_key_catalog_t *cat,uint32_t spn,uint32_t *ids,uint32_t cap) {
    uint32_t i,n=0;if(!cat || (cap && !ids))return 0;
    for(i=0;i<cat->count;++i){const j1939_signal_definition_t *d=&cat->metadata->signal_definitions[cat->entries[i].signal_definition];
        if((d->flags & J1939_META_HAS_SPN) && d->spn==spn){if(n<cap)ids[n]=i;++n;}}
    return n;
}
