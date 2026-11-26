#include "signal_keys.h"
#include "old_keys.h"
#include "new_keys.h"
#include "j1939_latest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define A SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED
#define TEMP SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE
#define LOAD SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD
#define B SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_SPEED
static const uint8_t stamp[32]=SUB_GENERATED_CATALOG_SHA256;
static const uint8_t old_stamp[32]=OLD_KEYS_CATALOG_SHA256,new_stamp[32]=NEW_KEYS_CATALOG_SHA256;
typedef struct {
    j1939_key_context_t *c;j1939_latest_t *latest;j1939_key_queue_t *queue;
    j1939_key_update_t saved;int status;
} app_t;
static void receive(void *user,const j1939_key_update_t *u) {
    app_t *a=(app_t *)user;j1939_latest_value_t value;
    a->saved=*u;a->status=j1939_latest_put(a->latest,a->c,u);
    assert(j1939_latest_get(a->latest,a->c,&a->c->identity,u->signal_id,u->domain,u->source_identity,&value)==J1939_SUB_BUSY);
    assert(j1939_latest_reset(a->latest,a->c)==J1939_SUB_BUSY);
    if(a->queue)assert(j1939_key_queue_push(a->queue,a->c,u)==0);
}
static void setup(j1939_key_context_t *c,const j1939_key_catalog_t *cat,const uint8_t *expected,
    j1939_key_layout_t l,j1939_sub_slot_t *base,j1939_key_slot_t *keys,uint16_t capacity,app_t *a) {
    assert(j1939_keys_init(c,cat,expected,l,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,keys,capacity,receive,a)==0);
}
static uint32_t subscribe(j1939_key_context_t *c,uint32_t id,j1939_key_filter_t f) {
    uint32_t token;assert(j1939_keys_subscribe(c,&c->identity,id,f,0,&token)==0);return token;
}
static void unchanged_failure(j1939_latest_t *s,j1939_key_context_t *c,uint32_t id,uint32_t d,uint32_t source,
                              int expected) {
    j1939_latest_value_t out,before;memset(&out,0xa5,sizeof(out));memcpy(&before,&out,sizeof(out));
    assert(j1939_latest_get(s,c,&c->identity,id,d,source,&out)==expected);
    assert(memcmp(&out,&before,sizeof(out))==0);
}
static void values_and_time(void) {
    j1939_key_context_t c;j1939_sub_slot_t base[8];j1939_key_slot_t keys[8];
    j1939_latest_t s;j1939_latest_slot_t slots[12];app_t app={.c=&c,.latest=&s};
    j1939_key_layout_t l={8,8,16};j1939_key_filter_t any={0,0,1,1},exact={1,11,0,0};
    uint8_t bytes[8]={0x34,0x12,0xfe,1,25,255,0,0};j1939_sub_timestamp_t ts={100,7};
    j1939_sub_frame_t f={0x18ff010b,bytes,&ts,8,1};j1939_latest_value_t v;
    uint32_t token;unsigned i;
    setup(&c,&generated_keys_catalog,stamp,l,base,keys,8,&app);
    assert(j1939_latest_init(&s,&c,slots,12)==0);
    unchanged_failure(&s,&c,A,1,11,J1939_LATEST_NO_VALUE);
    token=subscribe(&c,A,any);(void)subscribe(&c,TEMP,exact);(void)subscribe(&c,LOAD,exact);
    exact.source=62;(void)subscribe(&c,B,exact);exact.source=11;
    (void)subscribe(&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDEUNSIGNED_WIDE,exact);
    (void)subscribe(&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDESIGNED_WIDE,exact);
    (void)subscribe(&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_OVERFLOW_VALUE,exact);
    (void)subscribe(&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,exact);
    assert(j1939_keys_process(&c,1,&f,NULL)==0 && app.status==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,11,&v)==0 && v.raw==4660 && v.engineering==582.5);
    assert(!v.is_signed && strcmp(v.unit,"rpm")==0 && v.timestamp.nanoseconds==100 && v.quality==J1939_SUB_QUALITY_UNKNOWN);
    assert(j1939_latest_get(&s,&c,&c.identity,TEMP,1,11,&v)==0 && v.raw==254 && v.signed_raw==-2 && v.engineering==-41);
    assert(v.is_signed && strcmp(v.unit,"deg C")==0);
    assert(j1939_latest_get(&s,&c,&c.identity,LOAD,1,11,&v)==0 && v.engineering==10);
    /* Subscription validity does not imply age: app receives exact time context.
     * No layer here compares clocks, invents timestamps or rejects old readings. */
    ts.nanoseconds=0;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,11,&v)==0 && v.has_timestamp && v.timestamp.nanoseconds==0);
    ts.nanoseconds=UINT64_MAX;ts.clock_id=9;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,11,&v)==0 && v.timestamp.nanoseconds==UINT64_MAX && v.timestamp.clock_id==9);
    f.timestamp=NULL;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,11,&v)==0 && !v.has_timestamp);
    f.timestamp=&ts;ts.nanoseconds=100;ts.clock_id=7;
    /* Inactive mux produces no new observation: previous LOAD remains. */
    bytes[3]=0;bytes[4]=99;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,LOAD,1,11,&v)==0 && v.raw==25);
    bytes[0]=16;bytes[1]=0;f.can_id=0x18ff010c;assert(j1939_keys_process(&c,2,&f,NULL)==0);
    bytes[0]=24;f.can_id=0x18ff010d;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,2,12,&v)==0 && v.raw==16 && v.engineering==2);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,13,&v)==0 && v.raw==24 && v.engineering==3);
    f.can_id=0x18ff013e;bytes[0]=8;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,B,1,62,&v)==0 && v.engineering==2);
    unchanged_failure(&s,&c,A,1,62,J1939_SUB_INVALID);
    unchanged_failure(&s,&c,UINT32_MAX,1,11,J1939_SUB_INVALID);
    unchanged_failure(&s,&c,A,1,256,J1939_SUB_INVALID);
    assert(j1939_keys_unsubscribe(&c,token)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,A,1,11,&v)==0 && v.raw==4660);
    assert(j1939_latest_forget(&s,&c,&c.identity,A,1,11)==0);
    unchanged_failure(&s,&c,A,1,11,J1939_LATEST_NO_VALUE);
    /* Exact wide bits and signed minimum; nonfinite conversion stays explicit. */
    memset(bytes,255,8);f.can_id=0x18ff030b;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDEUNSIGNED_WIDE,1,11,&v)==0 && v.raw==UINT64_MAX);
    memset(bytes,0,8);bytes[7]=128;f.can_id=0x18ff050b;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDESIGNED_WIDE,1,11,&v)==0 && v.signed_raw==INT64_MIN);
    bytes[0]=2;f.can_id=0x18ff060b;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_OVERFLOW_VALUE,1,11,&v)==0 && !v.engineering_finite);
    memset(bytes,0,8);bytes[1]=50;f.can_id=0x18ff020b;f.timestamp=&ts;ts.nanoseconds=UINT64_MAX;
    assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,1,11,&v)==0 && v.engineering==95 && v.timestamp.nanoseconds==UINT64_MAX);
    ts.nanoseconds=0;assert(j1939_keys_process(&c,1,&f,NULL)==0);
    assert(j1939_latest_get(&s,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,1,11,&v)==0 && v.timestamp.nanoseconds==0);
    /* Corrupt stored bits and generation are rejected before conversion. */
    for(i=0;i<s.capacity;++i)if(slots[i].generation && slots[i].record.packed_key==app.saved.packed_key)break;
    assert(i<s.capacity);slots[i].record.raw=UINT64_MAX;
    unchanged_failure(&s,&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,1,11,J1939_SUB_INVALID);
    slots[i].record.raw=50;slots[i].generation++;
    unchanged_failure(&s,&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,1,11,J1939_KEY_MISMATCH);
    puts("PASS latest: identity/source selection, exact signed/unsigned/scaled/mux values, independent keys, timestamp presence/clock/extremes (age policy outside core), unsubscribe and corrupt storage");
}
/* All policy reads preserve the store, and failed reads preserve output bytes. */
static void policy_result(j1939_latest_t *s,j1939_key_context_t *c,
    const j1939_key_identity_t *expected,const j1939_latest_freshness_t *policy,int status) {
    j1939_latest_t before=*s;j1939_latest_slot_t slot=s->slots[0];
    j1939_latest_value_t out,sentinel;memset(&out,0xa5,sizeof(out));memcpy(&sentinel,&out,sizeof(out));
    assert(j1939_latest_get_with_policy(s,c,expected,A,1,11,policy,&out)==status);
    assert(memcmp(&before,s,sizeof(before))==0 && memcmp(&slot,&s->slots[0],sizeof(slot))==0);
    if(status)assert(memcmp(&out,&sentinel,sizeof(out))==0);
    else assert(out.raw==4660 && out.engineering==582.5 && strcmp(out.unit,"rpm")==0);
}
static void freshness(void) {
    j1939_key_context_t c;j1939_sub_slot_t base[1];j1939_key_slot_t keys[1];
    j1939_latest_t store;j1939_latest_slot_t slot;app_t app={.c=&c,.latest=&store};
    j1939_key_layout_t l={8,8,16};j1939_key_filter_t f={1,11,0,0};
    uint8_t bytes[8]={0x34,0x12};j1939_sub_timestamp_t ts={100,7};
    j1939_sub_frame_t frame={0x18ff010b,bytes,&ts,8,1};
    j1939_latest_freshness_t policy={{104,7},5};j1939_key_identity_t wrong;
    setup(&c,&generated_keys_catalog,stamp,l,base,keys,1,&app);
    assert(j1939_latest_init(&store,&c,&slot,1)==0);(void)subscribe(&c,A,f);
    policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_NO_VALUE);
    assert(j1939_keys_process(&c,1,&frame,NULL)==0);
    policy_result(&store,&c,&c.identity,&policy,0); /* age4 <5 */
    policy.now.nanoseconds=105;policy_result(&store,&c,&c.identity,&policy,0); /* inclusive boundary */
    policy.now.nanoseconds=106;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_STALE);
    policy_result(&store,&c,&c.identity,NULL,0); /* unchecked still validates identity */
    policy.max_age_ns=0;policy.now.nanoseconds=100;policy_result(&store,&c,&c.identity,&policy,0);
    policy.now.nanoseconds=101;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_STALE);
    policy.now.nanoseconds=99;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_TIME_REVERSED);
    policy.now.clock_id=8;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_CLOCK_MISMATCH);
    wrong=c.identity;wrong.catalog_sha256[0]^=1;
    policy_result(&store,&c,&wrong,&policy,J1939_KEY_MISMATCH);
    policy_result(&store,&c,&wrong,NULL,J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.layout=(j1939_key_layout_t){2,10,20};
    policy_result(&store,&c,&wrong,&policy,J1939_KEY_MISMATCH);
    policy_result(&store,&c,&wrong,NULL,J1939_KEY_MISMATCH);
    frame.timestamp=NULL;assert(j1939_keys_process(&c,1,&frame,NULL)==0);
    policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_NO_TIMESTAMP);
    policy_result(&store,&c,&c.identity,NULL,0);
    frame.timestamp=&ts;ts.nanoseconds=0;assert(j1939_keys_process(&c,1,&frame,NULL)==0);
    policy=(j1939_latest_freshness_t){{UINT64_MAX,7},UINT64_MAX};
    policy_result(&store,&c,&c.identity,&policy,0); /* full uint64 age, no addition */
    policy.max_age_ns=UINT64_MAX-1;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_STALE);
    ts.nanoseconds=UINT64_MAX;assert(j1939_keys_process(&c,1,&frame,NULL)==0);
    policy.max_age_ns=0;policy_result(&store,&c,&c.identity,&policy,0);
    policy.now.nanoseconds=UINT64_MAX-1;policy_result(&store,&c,&c.identity,&policy,J1939_LATEST_TIME_REVERSED);
    slot.record.raw=UINT64_MAX; /* invalid storage wins before any age decision */
    policy_result(&store,&c,&c.identity,&policy,J1939_SUB_INVALID);
    policy_result(&store,&c,&c.identity,NULL,J1939_SUB_INVALID);
    puts("PASS optional freshness: fresh/inclusive/stale/zero, missing time, clock mismatch/backwards, uint64 extremes, checked+unchecked identity and failure preservation");
}
static void consume_old(void *user,j1939_key_queue_t *q,const j1939_key_record_t *r,uint64_t epoch) {
    unsigned *count=(unsigned *)user;j1939_sub_ref_t ref;
    assert(j1939_key_queue_descriptor(q,epoch,r,&ref)==0);
    assert(j1939_signal_phys(&ref.metadata->db->signals[0],r->raw)==7);
    assert(strcmp(j1939_signal_unit(ref.metadata,0),"old unit")==0);++*count;
}
static void changes_and_failures(void) {
    j1939_key_context_t c[2];j1939_sub_slot_t base[2][1];j1939_key_slot_t keys[2][1];
    j1939_latest_t s,before;j1939_latest_slot_t slot,saved_slot;
    j1939_key_queue_t q[2];j1939_key_queue_metadata_t blobs[2];j1939_key_record_t records[2][2];
    j1939_key_pair_t pair;uint64_t g;j1939_latest_value_t value;
    app_t apps[2]={{.c=&c[0],.latest=&s,.queue=&q[0]},{.c=&c[1],.latest=&s,.queue=&q[1]}};
    j1939_key_layout_t layout={8,8,16};j1939_key_filter_t exact={1,11,0,0};
    const j1939_key_catalog_t *cats[]={&old_keys_catalog,&new_keys_catalog};const uint8_t *stamps[]={old_stamp,new_stamp};
    uint8_t bytes[8]={7};j1939_sub_timestamp_t ts={0,7};j1939_sub_frame_t f={0x18ff100b,bytes,&ts,8,1};unsigned i;
    for(i=0;i<2;++i){setup(&c[i],cats[i],stamps[i],layout,base[i],keys[i],1,&apps[i]);
        (void)subscribe(&c[i],0,exact);assert(j1939_key_queue_metadata_init(&blobs[i],&c[i])==0);
        assert(j1939_key_queue_init(&q[i],&blobs[i],records[i],2)==0);}
    assert(j1939_latest_init(&s,&c[0],&slot,1)==0);
    assert(j1939_key_pair_init(&pair,&q[0],&c[0],&q[1],&c[1])==0);
    assert(j1939_key_pair_process(&pair,1,&f,NULL)==0 && apps[0].status==0);
    assert(j1939_latest_get(&s,&c[0],&c[0].identity,0,1,11,&value)==0 && value.engineering==7 && strcmp(value.unit,"old unit")==0);
    assert(j1939_key_pair_switch(&pair)==0);
    {j1939_latest_value_t sentinel,out;const j1939_latest_freshness_t policy={{0,7},0};
     memset(&out,0xa5,sizeof(out));memcpy(&sentinel,&out,sizeof(out));
     assert(j1939_latest_get_with_policy(&s,pair.producers[pair.active],&pair.producers[pair.active]->identity,0,1,11,&policy,&out)==J1939_KEY_MISMATCH);
     assert(memcmp(&out,&sentinel,sizeof(out))==0);}
    unchanged_failure(&s,pair.producers[pair.active],0,1,11,J1939_KEY_MISMATCH);
    assert(j1939_key_pair_process(&pair,1,&f,NULL)==0 && apps[1].status==J1939_KEY_MISMATCH);
    /* Old bank's record still resolves with its retained old catalog. */
    assert(j1939_key_pair_drain(&pair,0,(j1939_key_consumer_t)NULL,NULL,NULL)==J1939_SUB_INVALID);
    assert(q[0].count==1 && q[1].count==1);
    {unsigned consumed=0;
     assert(j1939_key_pair_drain(&pair,1,consume_old,&consumed,NULL)==0 && consumed==1);}
    g=s.generation;assert(j1939_latest_reset(&s,&c[1])==0 && s.generation==g+1);
    unchanged_failure(&s,&c[1],0,1,11,J1939_LATEST_NO_VALUE);
    before=s;saved_slot=slot;
    assert(j1939_latest_put(&s,&c[1],&apps[0].saved)==J1939_KEY_MISMATCH);
    assert(memcmp(&s,&before,sizeof(s))==0 && memcmp(&slot,&saved_slot,sizeof(slot))==0);
    /* Queue would overflow here: stop event delivery explicitly, keep cache. */
    apps[1].queue=NULL;
    assert(j1939_key_pair_process(&pair,1,&f,NULL)==0 && apps[1].status==0);
    assert(j1939_latest_get(&s,&c[1],&c[1].identity,0,1,11,&value)==0 && value.raw==7 && value.engineering==14 && strcmp(value.unit,"new unit")==0);
    {const j1939_latest_freshness_t policy={{0,7},0};
     assert(j1939_latest_get_with_policy(&s,&c[1],&c[1].identity,0,1,11,&policy,&value)==0 && value.engineering==14 && strcmp(value.unit,"new unit")==0);}
    saved_slot=slot;f.size=1;
    assert(j1939_key_pair_process(&pair,1,&f,NULL)==J1939_SUB_INVALID && memcmp(&slot,&saved_slot,sizeof(slot))==0);f.size=8;
    /* Second exact source cannot evict the existing value at capacity. */
    (void)j1939_keys_unsubscribe(&c[1],1);exact.any_source=1;exact.source=0;(void)subscribe(&c[1],0,exact);
    f.can_id=0x18ff100c;assert(j1939_key_pair_process(&pair,1,&f,NULL)==0 && apps[1].status==J1939_SUB_FULL);
    assert(memcmp(&slot,&saved_slot,sizeof(slot))==0);
    f.can_id=0x18ff100b;bytes[0]=9;
    assert(j1939_key_pair_process(&pair,1,&f,NULL)==0 && apps[1].status==0);
    assert(j1939_latest_get(&s,&c[1],&c[1].identity,0,1,11,&value)==0 && value.engineering==18);
    before=s;assert(j1939_latest_init(&s,&c[1],NULL,1)==J1939_SUB_INVALID && memcmp(&s,&before,sizeof(s))==0);
    /* Retire pair: no subsequent pair calls; abandon its remaining event after quiescence. */
    /* Rebind the SAME producer object to a new layout; old store rejects first. */
    layout=(j1939_key_layout_t){2,10,20};setup(&c[1],&new_keys_catalog,new_stamp,layout,base[1],keys[1],1,&apps[1]);
    unchanged_failure(&s,&c[1],0,1,11,J1939_KEY_MISMATCH);
    {const j1939_latest_freshness_t policy={{0,7},0};j1939_latest_value_t out,sentinel;
     memset(&out,0xa5,sizeof(out));memcpy(&sentinel,&out,sizeof(out));
     assert(j1939_latest_get_with_policy(&s,&c[1],&c[1].identity,0,1,11,&policy,&out)==J1939_KEY_MISMATCH);
     assert(memcmp(&out,&sentinel,sizeof(out))==0);}
    assert(j1939_latest_reset(&s,&c[1])==0);
    (void)subscribe(&c[1],0,exact);
    assert(j1939_keys_process(&c[1],1,&f,NULL)==0 && apps[1].status==0);
    s.generation=UINT64_MAX;slot.generation=UINT64_MAX;before=s;saved_slot=slot;
    assert(j1939_latest_reset(&s,&c[1])==J1939_SUB_ID_LIMIT && memcmp(&before,&s,sizeof(s))==0 && memcmp(&saved_slot,&slot,sizeof(slot))==0);
    assert(j1939_latest_get(&s,&c[1],&c[1].identity,0,1,11,&value)==0 && value.engineering==18);
    puts("PASS latest: same keys/different catalogs, old queue remains interpretable, reset/rebind/layout checks, failed enqueue/full/init/reset preserve storage and generation exhaustion");
}
static void logical_and_empty(void) {
    j1939_key_context_t c;j1939_sub_slot_t base[2];j1939_key_slot_t keys[2];
    j1939_latest_t store;j1939_latest_slot_t slots[2],before[2];j1939_latest_value_t v;
    app_t app={.c=&c,.latest=&store};j1939_key_layout_t layout={2,10,20};
    const j1939_key_mapping_t maps[]={{1,700,11}},moved[]={{1,700,12}};
    j1939_key_filter_t f={1,700,0,0};uint8_t bytes[8]={8,0,0,1,0,255,0,0};
    j1939_sub_timestamp_t ts={0,7};j1939_sub_frame_t frame={0x18ff010b,bytes,&ts,8,1};
    assert(j1939_keys_init(&c,&generated_keys_catalog,stamp,layout,J1939_KEY_LOGICAL_ECU,99,maps,1,base,keys,2,receive,&app)==0);
    (void)subscribe(&c,A,f);(void)subscribe(&c,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_STATUS,f);
    assert(j1939_latest_init(&store,&c,slots,2)==0);
    assert(j1939_keys_process(&c,1,&frame,NULL)==0 && app.status==0);
    assert(j1939_latest_get(&store,&c,&c.identity,A,1,700,&v)==0 && v.raw==8);
    assert(j1939_latest_get(&store,&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_STATUS,1,700,&v)==0 && v.raw==255 && strcmp(v.unit,"")==0 && v.quality==J1939_SUB_QUALITY_UNKNOWN);
    memcpy(before,slots,sizeof(slots));
    assert(j1939_keys_init(&c,&generated_keys_catalog,stamp,layout,J1939_KEY_LOGICAL_ECU,99,moved,1,base,keys,2,receive,&app)==0);
    unchanged_failure(&store,&c,A,1,700,J1939_KEY_MISMATCH);
    assert(memcmp(before,slots,sizeof(slots))==0);
    assert(j1939_latest_reset(&store,&c)==0);(void)subscribe(&c,A,f);
    unchanged_failure(&store,&c,A,1,700,J1939_LATEST_NO_VALUE);
    frame.can_id=0x18ff010c;assert(j1939_keys_process(&c,1,&frame,NULL)==0);
    assert(j1939_latest_get(&store,&c,&c.identity,A,1,700,&v)==0);
    slots[0].record.can_id=0x18ff010b;
    unchanged_failure(&store,&c,A,1,700,J1939_SUB_INVALID);
    /* Fresh incarnation after retiring all prior results/units; zero capacity. */
    assert(j1939_latest_init(&store,&c,NULL,0)==0);
    assert(j1939_keys_process(&c,1,&frame,NULL)==0 && app.status==J1939_SUB_FULL);
    unchanged_failure(&store,&c,A,1,700,J1939_LATEST_NO_VALUE);
    puts("PASS latest: logical-source address changes, empty units, unknown reserved quality, actual-address validation and zero capacity");
}
int main(void){values_and_time();changes_and_failures();logical_and_empty();freshness();puts("4 latest-value scenario groups passed");return 0;}
