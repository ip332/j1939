#include "old_keys.h"
#include "new_keys.h"
#include "j1939_key_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t old_stamp[32]=OLD_KEYS_CATALOG_SHA256,new_stamp[32]=NEW_KEYS_CATALOG_SHA256;
typedef struct { j1939_key_context_t *context;j1939_key_queue_t *queue;j1939_key_pair_t *pair;int status; } producer_t;
typedef struct { double values[8];char units[8][16];uint64_t generations[8];unsigned count;j1939_key_pair_t *pair; } consumer_t;
static void enqueue(void *user,const j1939_key_update_t *update) {
    producer_t *p=(producer_t *)user;p->status=j1939_key_queue_push(p->queue,p->context,update);
    assert(j1939_key_pair_switch(p->pair)==J1939_SUB_BUSY);
}
static void consume(void *user,j1939_key_queue_t *queue,const j1939_key_record_t *r,uint64_t generation) {
    consumer_t *out=(consumer_t *)user;j1939_sub_ref_t ref;const j1939_signal_t *signal;uint16_t index;
    assert(j1939_key_pair_switch(out->pair)==J1939_SUB_BUSY);
    assert(j1939_key_queue_descriptor(queue,generation,r,&ref)==0);
    index=ref.metadata->signal_definitions[ref.signal_definition].signal_index;signal=&ref.metadata->db->signals[index];
    out->values[out->count]=j1939_signal_phys(signal,r->raw);
    (void)snprintf(out->units[out->count],16,"%s",j1939_signal_unit(ref.metadata,index));
    out->generations[out->count++]=generation;
}
/* Saved updates are deliberately copied for negative lifetime tests. */
typedef struct {
    j1939_key_context_t *ctx;j1939_key_queue_t *q;
    j1939_key_update_t saved;int try_saved,status;
} lease_app_t;
static void leased_receive(void *user,const j1939_key_update_t *u) {
    lease_app_t *a=(lease_app_t *)user;
    if(a->try_saved) {
        j1939_key_queue_t before=*a->q;
        j1939_key_record_t records_before[2];
        memcpy(records_before,a->q->records,sizeof(records_before));
        assert(j1939_key_queue_push(a->q,a->ctx,&a->saved)==J1939_KEY_MISMATCH);
        assert(memcmp(&before,a->q,sizeof(before))==0);
        assert(memcmp(records_before,a->q->records,sizeof(records_before))==0);
    } else a->saved=*u;
    a->status=j1939_key_queue_push(a->q,a->ctx,u);
}
static void bind(j1939_key_context_t *c,const j1939_key_catalog_t *catalog,const uint8_t *stamp,
    j1939_sub_slot_t *base,j1939_key_slot_t *slots,lease_app_t *app) {
    uint32_t token;j1939_key_layout_t l={8,8,16};j1939_key_filter_t f={1,11,0,0};
    assert(j1939_keys_init(c,catalog,stamp,l,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,leased_receive,app)==0);
    assert(j1939_keys_subscribe(c,&c->identity,0,f,0,&token)==0);
}
static void regressions(void) {
    j1939_key_context_t c[2];j1939_sub_slot_t base[2][1];j1939_key_slot_t slots[2][1];
    j1939_key_queue_t q[2],before;j1939_key_queue_metadata_t m[2],fresh;
    j1939_key_record_t records[2][2]={0},saved,now,contents[2];uint64_t epoch,g;
    lease_app_t apps[2]={{0}};j1939_key_pair_t pair,pair_before;j1939_sub_ref_t ref;
    uint8_t bytes[8]={7};j1939_sub_frame_t frame={0x18ff100b,bytes,NULL,8,1};unsigned i;
    for(i=0;i<2;++i) {
        apps[i].ctx=&c[i];apps[i].q=&q[i];bind(&c[i],&old_keys_catalog,old_stamp,base[i],slots[i],&apps[i]);
        assert(j1939_key_queue_metadata_init(&m[i],&c[i])==0);
        assert(j1939_key_queue_init(&q[i],&m[i],records[i],2)==0);
    }
    assert(j1939_key_queue_replace(&q[1],&m[1])==0 && q[1].generation==2);
    assert(j1939_keys_process(&c[1],1,&frame,NULL)==0);
    assert(j1939_key_queue_pop(&q[1],&saved,&epoch)==0 && epoch==2);
    assert(j1939_key_queue_descriptor(&q[1],epoch,&saved,&ref)==0);
    assert(j1939_signal_phys(&ref.metadata->db->signals[0],saved.raw)==7);
    bind(&c[1],&new_keys_catalog,new_stamp,base[1],slots[1],&apps[1]);
    assert(j1939_key_queue_metadata_init(&fresh,&c[1])==0);
    assert(j1939_key_queue_replace(&q[1],&fresh)==0 && q[1].generation==3);
    assert(c[1].current_update==NULL);
    before=q[1];memcpy(contents,records[1],sizeof(contents));
    assert(j1939_key_queue_push(&q[1],&c[1],&apps[1].saved)==J1939_KEY_MISMATCH);
    assert(memcmp(&before,&q[1],sizeof(before))==0 && memcmp(contents,records[1],sizeof(contents))==0);
    apps[1].try_saved=1;
    assert(j1939_keys_process(&c[1],1,&frame,NULL)==0 && apps[1].status==0);
    assert(j1939_key_queue_pop(&q[1],&now,&g)==0 && now.packed_key==saved.packed_key);
    assert(j1939_key_queue_descriptor(&q[1],g,&now,&ref)==0);
    assert(j1939_signal_phys(&ref.metadata->db->signals[0],now.raw)==14);
    assert(strcmp(j1939_signal_unit(ref.metadata,0),"new unit")==0);
    assert(j1939_key_queue_descriptor(&q[1],epoch,&saved,&ref)==J1939_KEY_MISMATCH);
    assert(j1939_key_pair_init(&pair,&q[0],&c[0],&q[1],&c[1])==0 && pair.generation==3);
    assert(j1939_key_pair_switch(&pair)==0 && q[1].generation==4);
    assert(j1939_key_queue_descriptor(&q[1],epoch,&saved,&ref)==J1939_KEY_MISMATCH);
    /* Empty banks and repeated switches must advance, never recycle epochs. */
    for(i=0;i<6;++i){g=pair.generation;assert(j1939_key_pair_switch(&pair)==0 && pair.generation==g+1);}
    /* Prepared bank may advance beyond the pair's last published generation. */
    {uint8_t bank;assert(j1939_key_pair_reusable(&pair,&bank)==0);
     for(i=0;i<3;++i)assert(j1939_key_queue_replace(&q[bank],q[bank].metadata)==0);
     g=q[bank].generation;assert(j1939_key_pair_switch(&pair)==0 && pair.generation==g+1);}
    /* Last usable generation succeeds; overflow rejects without changing data. */
    pair.generation=UINT64_MAX-1;
    assert(j1939_key_pair_switch(&pair)==0 && pair.generation==UINT64_MAX);
    pair_before=pair;before=q[1];memcpy(contents,records[1],sizeof(contents));
    assert(j1939_key_pair_switch(&pair)==J1939_SUB_ID_LIMIT);
    assert(memcmp(&pair,&pair_before,sizeof(pair))==0 && memcmp(&before,&q[1],sizeof(before))==0);
    assert(memcmp(contents,records[1],sizeof(contents))==0);
    /* Exhaustion must also inspect bank epochs when pair epoch is lower. */
    pair.generation=1;pair_before=pair;
    assert(j1939_key_pair_switch(&pair)==J1939_SUB_ID_LIMIT);
    assert(memcmp(&pair,&pair_before,sizeof(pair))==0);
    /* Initialization also accounts for the exhausted second bank. */
    q[1].generation=UINT64_MAX;
    assert(j1939_key_pair_init(&pair,&q[0],&c[0],&q[1],&c[1])==J1939_SUB_ID_LIMIT);
    assert(memcmp(&pair,&pair_before,sizeof(pair))==0);
    before=q[1];assert(j1939_key_queue_replace(&q[1],q[1].metadata)==J1939_SUB_ID_LIMIT);
    assert(memcmp(&before,&q[1],sizeof(before))==0);
    puts("PASS generation adoption/reuse regression, stale callback copies, same keys across catalogs, repeated/empty switches and transactional exhaustion");
}
int main(void) {
    j1939_key_context_t contexts[2];j1939_sub_slot_t base[2][1];j1939_key_slot_t filters[2][1];
    j1939_key_queue_metadata_t metadata[2];j1939_key_queue_t queues[2];j1939_key_record_t records[2][2];j1939_key_pair_t pair;
    producer_t producers[2];consumer_t consumer={0};j1939_key_layout_t layout={8,8,16};j1939_key_filter_t exact={1,11,0,0};
    const j1939_key_catalog_t *catalogs[]={&old_keys_catalog,&new_keys_catalog};const uint8_t *stamps[]={old_stamp,new_stamp};
    const uint32_t ids[]={OLD_KEYS_ID_TRANSITION_EXAMPLE_VALUE,NEW_KEYS_ID_TRANSITION_EXAMPLE_VALUE};
    uint8_t bytes[8]={7},bank=255;j1939_sub_frame_t frame={0x18ff100b,bytes,NULL,8,1};uint16_t n;unsigned i;
    assert(ids[0]==ids[1] && ids[0]==0 && memcmp(catalogs[0]->sha256,catalogs[1]->sha256,32)!=0);
    for(i=0;i<2;++i){uint32_t token;producers[i]=(producer_t){&contexts[i],&queues[i],&pair,0};
        assert(j1939_keys_init(&contexts[i],catalogs[i],stamps[i],layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base[i],filters[i],1,enqueue,&producers[i])==0);
        assert(j1939_keys_subscribe(&contexts[i],&contexts[i].identity,ids[i],exact,0,&token)==0);
        assert(j1939_key_queue_metadata_init(&metadata[i],&contexts[i])==0);
        assert(j1939_key_queue_init(&queues[i],&metadata[i],records[i],2)==0);}
    assert(j1939_key_pair_init(&pair,&queues[0],&contexts[0],&queues[1],&contexts[1])==0);consumer.pair=&pair;
    assert(j1939_key_pair_reusable(&pair,&bank)==0 && bank==1);
    assert(j1939_key_pair_process(&pair,1,&frame,&n)==0 && n==1 && queues[0].count==1);
    assert(j1939_key_pair_switch(&pair)==0 && pair.generation==2);
    assert(j1939_key_pair_process(&pair,1,&frame,&n)==0 && queues[1].count==1);
    {j1939_key_pair_t saved_pair=pair;j1939_key_queue_t saved_queues[2];j1939_key_record_t saved_records[2][2];
     memcpy(saved_queues,queues,sizeof(queues));memcpy(saved_records,records,sizeof(records));
     assert(j1939_key_pair_switch(&pair)==J1939_SUB_BUSY && queues[0].count==1 && queues[1].count==1);
     assert(memcmp(&saved_pair,&pair,sizeof(pair))==0 && memcmp(saved_queues,queues,sizeof(queues))==0);
     assert(memcmp(saved_records,records,sizeof(records))==0);}
    assert(j1939_key_pair_reusable(&pair,&bank)==J1939_SUB_BUSY);
    assert(j1939_key_pair_drain(&pair,1,consume,&consumer,&n)==0 && n==1);
    assert(consumer.values[0]==7 && strcmp(consumer.units[0],"old unit")==0 && consumer.generations[0]==1);
    assert(j1939_key_pair_reusable(&pair,&bank)==0 && bank==0);
    assert(j1939_key_pair_switch(&pair)==0 && pair.generation==3);
    assert(j1939_key_pair_process(&pair,1,&frame,&n)==0);
    assert(j1939_key_pair_drain(&pair,2,consume,&consumer,&n)==0 && n==2);
    assert(consumer.values[1]==14 && strcmp(consumer.units[1],"new unit")==0 && consumer.generations[1]==2);
    assert(consumer.values[2]==7 && consumer.generations[2]==3 && queues[0].count==0 && queues[1].count==0);
    assert(!queues[0].dropped && !queues[1].dropped && !queues[0].invalidated && !queues[1].invalidated);
    puts("PASS two real catalog generations, identical signal IDs/keys, old/new descriptor lookup, FIFO handoff with no transition loss");
    for(i=0;i<3;++i)assert(j1939_key_pair_process(&pair,1,&frame,&n)==0);
    assert(queues[0].count==2 && queues[0].dropped==1 && producers[0].status==J1939_SUB_FULL);
    pair.generation=UINT64_MAX;assert(j1939_key_pair_switch(&pair)==J1939_SUB_ID_LIMIT && queues[0].count==2);
    puts("PASS third-update backpressure, frame-boundary/reentrancy guards, explicit bounded overflow and generation exhaustion");
    regressions();
    puts("3 double-buffer scenario groups passed");return 0;
}
