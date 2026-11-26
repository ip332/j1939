#include "j1939_key_queue.h"
static uint32_t add_count(uint32_t a,uint32_t b) { return b>UINT32_MAX-a?UINT32_MAX:a+b; }
static int blob_ok(const j1939_key_queue_metadata_t *m) {
    return m && m->producer && m->producer->catalog &&
        j1939_key_identity_equal(&m->identity,&m->producer->identity);
}
static int invalidate(j1939_key_queue_t *q) {
    if(!q->stale){q->invalidated=add_count(q->invalidated,q->count);q->count=0;q->read_index=0;
        if(q->generation!=UINT64_MAX)++q->generation;
        q->stale=1;
    }
    return J1939_KEY_MISMATCH;
}
static int check(j1939_key_queue_t *q) {
    if(!q || !q->metadata || (q->capacity && !q->records))return J1939_SUB_INVALID;
    if(q->stale || !blob_ok(q->metadata))return invalidate(q);
    return 0;
}
int j1939_key_queue_metadata_init(j1939_key_queue_metadata_t *m,const j1939_key_context_t *c) {
    j1939_key_queue_metadata_t value;
    if(!m || !c || !c->catalog)return J1939_SUB_INVALID;
    if(c->busy)return J1939_SUB_BUSY;
    value.producer=c;value.identity=c->identity;*m=value;return 0;
}
int j1939_key_queue_init(j1939_key_queue_t *q,const j1939_key_queue_metadata_t *m,
                         j1939_key_record_t *records,uint16_t capacity) {
    j1939_key_queue_t value={0};
    if(!q || !blob_ok(m) || (capacity && !records))return J1939_SUB_INVALID;
    if(m->producer->busy)return J1939_SUB_BUSY;
    value.metadata=m;value.records=records;value.capacity=capacity;value.generation=1;*q=value;return 0;
}
int j1939_key_queue_replace(j1939_key_queue_t *q,const j1939_key_queue_metadata_t *m) {
    if(!q || !q->metadata || !blob_ok(m))return J1939_SUB_INVALID;
    if(q->metadata->producer->busy || m->producer->busy)return J1939_SUB_BUSY;
    if(q->generation==UINT64_MAX)return J1939_SUB_ID_LIMIT;
    q->invalidated=add_count(q->invalidated,q->count);q->count=0;q->read_index=0;
    ++q->generation;q->metadata=m;q->stale=0;return 0;
}
int j1939_key_queue_push(j1939_key_queue_t *q,const j1939_key_context_t *producer,const j1939_key_update_t *u) {
    int status;uint32_t index;j1939_key_record_t record={0};
    if(!u || !producer || !q || !q->metadata)return J1939_SUB_INVALID;
    /* Reject saved/copied/foreign updates before any queue mutation, even if
     * the same producer storage has since been rebound to another catalog. */
    if(producer!=q->metadata->producer || !producer->busy || producer->current_update!=u)
        return J1939_KEY_MISMATCH;
    status=check(q);if(status)return status;
    if(q->count==q->capacity){q->dropped=add_count(q->dropped,1);return J1939_SUB_FULL;}
    record.raw=u->value.raw;record.packed_key=u->packed_key;record.can_id=u->value.can_id;
    record.subscription_id=u->value.subscription_id;record.tag=u->value.tag;
    record.has_timestamp=u->value.has_timestamp;record.quality=u->value.quality;
    if(record.has_timestamp){record.nanoseconds=u->value.timestamp.nanoseconds;record.clock_id=u->value.timestamp.clock_id;}
    index=((uint32_t)q->read_index+q->count)%q->capacity;q->records[index]=record;++q->count;return 0;
}
int j1939_key_queue_pop(j1939_key_queue_t *q,j1939_key_record_t *record,uint64_t *generation) {
    int status;j1939_sub_ref_t ref;
    if(!record || !generation)return J1939_SUB_INVALID;
    status=check(q);if(status)return status;
    if(q->metadata->producer->busy)return J1939_SUB_BUSY;
    if(!q->count)return J1939_SUB_NOT_FOUND;
    status=j1939_keys_decode(q->metadata->producer,&q->metadata->identity,q->records[q->read_index].packed_key,&ref);
    if(status)return invalidate(q);
    *record=q->records[q->read_index];*generation=q->generation;
    q->read_index=(uint16_t)(((uint32_t)q->read_index+1)%q->capacity);--q->count;return 0;
}
int j1939_key_queue_descriptor(j1939_key_queue_t *q,uint64_t generation,
    const j1939_key_record_t *record,j1939_sub_ref_t *selection) {
    int status;if(!record || !selection)return J1939_SUB_INVALID;
    status=check(q);if(status)return status;
    if(generation!=q->generation)return J1939_KEY_MISMATCH;
    return j1939_keys_decode(q->metadata->producer,&q->metadata->identity,record->packed_key,selection);
}

static int pair_ok(const j1939_key_pair_t *p) {
    return p && p->queues[0] && p->queues[1] && p->producers[0] && p->producers[1] && p->active<2;
}
int j1939_key_pair_init(j1939_key_pair_t *p,j1939_key_queue_t *a,j1939_key_context_t *ac,
    j1939_key_queue_t *b,j1939_key_context_t *bc) {
    j1939_key_pair_t value={0};
    if(!p || !a || !b || a==b || !ac || !bc || ac==bc || !a->capacity || !b->capacity ||
        !a->records || !b->records || a->records==b->records || !blob_ok(a->metadata) || !blob_ok(b->metadata) ||
        a->metadata->producer!=ac || b->metadata->producer!=bc || b->count || a->stale || b->stale)return J1939_SUB_INVALID;
    if(ac->busy || bc->busy)return J1939_SUB_BUSY;
    value.queues[0]=a;value.queues[1]=b;value.producers[0]=ac;value.producers[1]=bc;value.generation=a->generation>b->generation?a->generation:b->generation;
    if(value.generation==UINT64_MAX)return J1939_SUB_ID_LIMIT;
    *p=value;return 0;
}
int j1939_key_pair_reusable(const j1939_key_pair_t *p,uint8_t *bank) {
    if(!pair_ok(p) || !bank)return J1939_SUB_INVALID;
    if(p->busy || p->has_draining || p->queues[1-p->active]->count)return J1939_SUB_BUSY;
    *bank=(uint8_t)(1-p->active);return 0;
}
int j1939_key_pair_switch(j1939_key_pair_t *p) {
    j1939_key_queue_t *next;uint8_t old,target;int status;uint64_t generation;
    if(!pair_ok(p))return J1939_SUB_INVALID;
    if(p->busy || p->has_draining || p->producers[0]->busy || p->producers[1]->busy)return J1939_SUB_BUSY;
    generation=p->generation;
    if(p->queues[0]->generation>generation)generation=p->queues[0]->generation;
    if(p->queues[1]->generation>generation)generation=p->queues[1]->generation;
    if(generation==UINT64_MAX)return J1939_SUB_ID_LIMIT;
    old=p->active;target=(uint8_t)(1-old);next=p->queues[target];
    if(next->count)return J1939_SUB_BUSY;
    status=check(next);if(status)return status;
    if(next->metadata->producer!=p->producers[target])return J1939_KEY_MISMATCH;
    p->generation=generation+1;next->generation=p->generation;p->active=target;p->has_draining=p->queues[old]->count!=0;return 0;
}
int j1939_key_pair_process(j1939_key_pair_t *p,uint32_t domain,const j1939_sub_frame_t *frame,uint16_t *count) {
    int status;if(!pair_ok(p))return J1939_SUB_INVALID;
    if(p->busy)return J1939_SUB_BUSY;
    p->busy=1;status=j1939_keys_process(p->producers[p->active],domain,frame,count);p->busy=0;return status;
}
int j1939_key_pair_drain(j1939_key_pair_t *p,uint16_t limit,j1939_key_consumer_t consume,void *user,uint16_t *count) {
    uint16_t n=0;int status=0;
    if(!pair_ok(p) || !consume)return J1939_SUB_INVALID;
    if(p->busy)return J1939_SUB_BUSY;
    p->busy=1;
    while(n<limit) {
        uint8_t bank=p->has_draining?(uint8_t)(1-p->active):p->active;
        j1939_key_queue_t *q=p->queues[bank];j1939_key_record_t record;uint64_t generation;
        if(!q->count){if(p->has_draining){p->has_draining=0;continue;}break;}
        status=j1939_key_queue_pop(q,&record,&generation);if(status)break;
        consume(user,q,&record,generation);++n;
        if(p->has_draining && !q->count)p->has_draining=0;
    }
    p->busy=0;if(count)*count=n;return status;
}
