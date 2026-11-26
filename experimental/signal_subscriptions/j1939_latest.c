#include "j1939_latest.h"
#include <float.h>
static int bound(const j1939_latest_t *s,const j1939_key_context_t *c,
                 const j1939_key_identity_t *expected) {
    if(!s || !c || !c->catalog || !s->generation || (s->capacity && !s->slots))return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    if(c!=s->producer || c->catalog!=s->catalog ||
       !j1939_key_identity_equal(&s->identity,&c->identity) ||
       !j1939_key_identity_equal(expected,&c->identity))return J1939_KEY_MISMATCH;
    return 0;
}
int j1939_latest_init(j1939_latest_t *s,const j1939_key_context_t *c,
                       j1939_latest_slot_t *slots,uint16_t capacity) {
    j1939_latest_t value={0};uint16_t i;
    if(!s || !c || !c->catalog || (capacity && !slots))return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    value.producer=c;value.catalog=c->catalog;value.identity=c->identity;
    value.slots=slots;value.capacity=capacity;value.generation=1;
    for(i=0;i<capacity;++i)slots[i]=(j1939_latest_slot_t){0};
    *s=value;return 0;
}
int j1939_latest_reset(j1939_latest_t *s,const j1939_key_context_t *c) {
    uint16_t i;
    if(!s || !s->generation || !c || !c->catalog || (s->capacity && !s->slots))return J1939_SUB_INVALID;
    if(c->busy || (s->producer && s->producer->busy))return J1939_SUB_BUSY;
    if(s->generation==UINT64_MAX)return J1939_SUB_ID_LIMIT;
    ++s->generation;s->producer=c;s->catalog=c->catalog;s->identity=c->identity;
    for(i=0;i<s->capacity;++i)s->slots[i]=(j1939_latest_slot_t){0};
    return 0;
}
int j1939_latest_put(j1939_latest_t *s,const j1939_key_context_t *c,const j1939_key_update_t *u) {
    uint16_t i,target; j1939_latest_slot_t value={0};
    if(!s || !c || !u || !c->catalog || !s->generation || (s->capacity && !s->slots))return J1939_SUB_INVALID;
    if(c!=s->producer || c->catalog!=s->catalog || !c->busy || c->current_update!=u ||
       !j1939_key_identity_equal(&s->identity,&c->identity))return J1939_KEY_MISMATCH;
    target=s->capacity;
    for(i=0;i<s->capacity;++i) {
        if(s->slots[i].generation && s->slots[i].generation!=s->generation)return J1939_SUB_INVALID;
        if(s->slots[i].generation && s->slots[i].record.packed_key==u->packed_key){target=i;break;}
        if(!s->slots[i].generation && target==s->capacity)target=i;
    }
    if(target==s->capacity)return J1939_SUB_FULL;
    value.generation=s->generation;value.record.raw=u->value.raw;value.record.packed_key=u->packed_key;
    value.record.can_id=u->value.can_id;value.record.subscription_id=u->value.subscription_id;value.record.tag=u->value.tag;
    value.record.has_timestamp=u->value.has_timestamp;value.record.quality=u->value.quality;
    if(u->value.has_timestamp){value.record.nanoseconds=u->value.timestamp.nanoseconds;value.record.clock_id=u->value.timestamp.clock_id;}
    s->slots[target]=value;return 0;
}
static int locate(const j1939_latest_t *s,const j1939_key_context_t *c,
    const j1939_key_identity_t *expected,uint32_t id,uint32_t domain,uint32_t source,
    uint16_t *index,j1939_sub_ref_t *ref) {
    uint32_t key;uint16_t i;int status=bound(s,c,expected);
    if(status)return status;
    status=j1939_key_pack(c->identity.layout,domain,source,id,&key);if(status)return status;
    status=j1939_keys_decode(c,expected,key,ref);
    if(status)return status==J1939_SUB_NOT_FOUND?J1939_SUB_INVALID:status;
    for(i=0;i<s->capacity;++i)if(s->slots[i].generation) {
        if(s->slots[i].generation!=s->generation)return J1939_KEY_MISMATCH;
        if(s->slots[i].record.packed_key==key){*index=i;return 0;}
    }
    return J1939_LATEST_NO_VALUE;
}
int j1939_latest_get_with_policy(const j1939_latest_t *s,const j1939_key_context_t *c,
    const j1939_key_identity_t *expected,uint32_t id,uint32_t domain,uint32_t source,
    const j1939_latest_freshness_t *policy,j1939_latest_value_t *out) {
    uint16_t index,si;uint32_t actual_source,i;uint64_t mask;int status;
    j1939_sub_ref_t ref;const j1939_key_record_t *r;const j1939_signal_t *signal;
    const j1939_message_t *message;j1939_latest_value_t value={0};
    if(!out)return J1939_SUB_INVALID;
    status=locate(s,c,expected,id,domain,source,&index,&ref);if(status)return status;
    r=&s->slots[index].record;
    si=ref.metadata->signal_definitions[ref.signal_definition].signal_index;
    signal=&ref.metadata->db->signals[si];
    message=&ref.metadata->db->messages[ref.metadata->message_definitions[ref.message_definition].message_index];
    mask=signal->length==64?UINT64_MAX:(UINT64_C(1)<<signal->length)-1;
    if(r->has_timestamp>1 || r->quality!=J1939_SUB_QUALITY_UNKNOWN || (r->raw & ~mask) ||
       r->can_id>UINT32_C(0x1fffffff) || (r->can_id & UINT32_C(0x02000000)) ||
       j1939_db_find(ref.metadata->db,r->can_id,1)!=message)return J1939_SUB_INVALID;
    actual_source=r->can_id & 255u;
    if(c->identity.source_mode==J1939_KEY_LOGICAL_ECU) {
        for(i=0;i<c->mapping_count;++i)if(c->mapping[i].domain==domain && c->mapping[i].address==actual_source)break;
        if(i==c->mapping_count)return J1939_SUB_INVALID;
        actual_source=c->mapping[i].logical_ecu;
    }
    if(actual_source!=source)return J1939_SUB_INVALID;
    if(policy) {
        if(!r->has_timestamp)return J1939_LATEST_NO_TIMESTAMP;
        if(policy->now.clock_id!=r->clock_id)return J1939_LATEST_CLOCK_MISMATCH;
        if(policy->now.nanoseconds<r->nanoseconds)return J1939_LATEST_TIME_REVERSED;
        /* Compare before subtracting: no unsigned underflow, no timestamp+age sum. */
        if(policy->now.nanoseconds-r->nanoseconds>policy->max_age_ns)return J1939_LATEST_STALE;
    }
    value.raw=r->raw;value.is_signed=(signal->flags & J1939_SIG_SIGNED)!=0;
    if(value.is_signed)value.signed_raw=(r->raw & (UINT64_C(1)<<(signal->length-1)))?
        -1-(int64_t)(~r->raw & mask):(int64_t)r->raw;
    value.engineering=j1939_signal_phys(signal,r->raw);
    value.engineering_finite=value.engineering<=DBL_MAX && value.engineering>=-DBL_MAX;
    value.unit=j1939_signal_unit(ref.metadata,si);value.quality=r->quality;
    value.has_timestamp=r->has_timestamp;
    if(r->has_timestamp){value.timestamp.nanoseconds=r->nanoseconds;value.timestamp.clock_id=r->clock_id;}
    *out=value;return 0;
}
int j1939_latest_get(const j1939_latest_t *s,const j1939_key_context_t *c,
    const j1939_key_identity_t *expected,uint32_t id,uint32_t domain,uint32_t source,
    j1939_latest_value_t *out) {
    return j1939_latest_get_with_policy(s,c,expected,id,domain,source,NULL,out);
}
int j1939_latest_forget(j1939_latest_t *s,const j1939_key_context_t *c,
    const j1939_key_identity_t *expected,uint32_t id,uint32_t domain,uint32_t source) {
    uint16_t index;j1939_sub_ref_t ref;int status=locate(s,c,expected,id,domain,source,&index,&ref);
    if(status)return status;
    s->slots[index]=(j1939_latest_slot_t){0};return 0;
}
