#include "signal_keys.h"
#include "j1939_key_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t expected_catalog[32]=SUB_GENERATED_CATALOG_SHA256;
typedef struct { j1939_key_context_t *context;j1939_key_queue_t *queue;int status; } app_t;
static void receive(void *user,const j1939_key_update_t *u) {
    app_t *app=(app_t *)user;app->status=j1939_key_queue_push(app->queue,app->context,u);
    assert(j1939_key_queue_replace(app->queue,app->queue->metadata)==J1939_SUB_BUSY);
}
int main(void) {
    j1939_key_context_t ctx;j1939_sub_slot_t base[2];j1939_key_slot_t slots[2];
    j1939_key_queue_metadata_t blob,replacement;j1939_key_queue_t q;j1939_key_record_t records[2],record,old_record;
    j1939_key_layout_t layout={8,8,16};j1939_key_filter_t filter={1,11,0,0};
    app_t app={&ctx,&q,0};uint32_t token;uint64_t generation,old_generation;
    uint8_t data[8]={0x34,0x12,0xfe,1,25,0,0,0};j1939_sub_timestamp_t ts={0,17};
    j1939_sub_frame_t frame={0x18ff010b,data,&ts,8,1};j1939_sub_ref_t ref;const j1939_signal_t *signal;
    assert(j1939_keys_init(&ctx,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,2,receive,&app)==0);
    assert(j1939_key_queue_metadata_init(&blob,&ctx)==0 && j1939_key_queue_init(&q,&blob,records,2)==0);
    assert(j1939_keys_subscribe(&ctx,&ctx.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE,filter,9,&token)==0);
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && q.count==1 && app.status==0);
    assert(j1939_key_queue_pop(&q,&record,&generation)==0 && record.raw==254 && record.tag==9);
    assert(record.has_timestamp && record.nanoseconds==0 && record.clock_id==17);
    assert(j1939_key_queue_descriptor(&q,generation,&record,&ref)==0);
    signal=&ref.metadata->db->signals[ref.metadata->signal_definitions[ref.signal_definition].signal_index];
    assert((signal->flags & J1939_SIG_SIGNED) && j1939_signal_phys(signal,record.raw)==-41);
    assert(strcmp(j1939_signal_unit(ref.metadata,ref.metadata->signal_definitions[ref.signal_definition].signal_index),"deg C")==0);
    old_record=record;old_generation=generation;
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && q.count==1);
    assert(j1939_key_queue_metadata_init(&replacement,&ctx)==0);
    assert(j1939_key_queue_replace(&q,&replacement)==0 && q.count==0 && q.invalidated==1 && q.generation!=old_generation);
    assert(j1939_key_queue_descriptor(&q,old_generation,&old_record,&ref)==J1939_KEY_MISMATCH);
    puts("PASS signal-ID descriptor lookup, timestamp/raw retention, same-identity replacement invalidates queue and old copies");
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && app.status==0);
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && app.status==0);
    {j1939_key_record_t saved[2];memcpy(saved,records,sizeof(saved));
     assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && app.status==J1939_SUB_FULL && q.dropped==1 && q.count==2);
     assert(memcmp(saved,records,sizeof(saved))==0);}
    {j1939_key_queue_t saved=q;
     assert(j1939_key_queue_init(&q,&blob,NULL,1)==J1939_SUB_INVALID);
     assert(memcmp(&saved,&q,sizeof(q))==0);}
    assert(j1939_key_queue_pop(&q,&record,&generation)==0);assert(j1939_key_queue_pop(&q,&record,&generation)==0);
    assert(j1939_key_queue_pop(&q,&record,&generation)==J1939_SUB_NOT_FOUND);
    puts("PASS bounded drop-newest queue, no background thread, callback-safe enqueue");
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && q.count==1);
    layout=(j1939_key_layout_t){2,10,20};
    assert(j1939_keys_init(&ctx,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,2,receive,&app)==0);
    assert(j1939_key_queue_pop(&q,&record,&generation)==J1939_KEY_MISMATCH && q.count==0 && q.stale);
    assert(j1939_key_queue_metadata_init(&blob,&ctx)==0);
    assert(j1939_key_queue_replace(&q,&blob)==0 && !q.stale);
    assert(j1939_keys_subscribe(&ctx,&ctx.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE,filter,9,&token)==0);
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && app.status==0 && q.count==1);
    assert(j1939_key_queue_pop(&q,&record,&generation)==0 && j1939_key_queue_descriptor(&q,generation,&record,&ref)==0);
    assert(record.raw==254 && record.quality==J1939_SUB_QUALITY_UNKNOWN);
    q.generation=UINT64_MAX;assert(j1939_key_queue_replace(&q,&blob)==J1939_SUB_ID_LIMIT);
    assert(j1939_key_queue_init(&q,&blob,NULL,0)==0);
    assert(j1939_keys_process(&ctx,1,&frame,NULL)==0 && app.status==J1939_SUB_FULL);
    puts("PASS automatic stale-binding invalidation, explicit rebind, generation exhaustion, zero capacity");
    assert(sizeof(j1939_key_record_t)==40);
    puts("3 compact-queue scenario groups passed; record40 bytes, zero descriptors per record");return 0;
}
