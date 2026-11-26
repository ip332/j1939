#include "j1939_subscriptions.h"
#include "subscriptions.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

typedef struct {
    j1939_sub_update_t updates[128];
    unsigned count;
    j1939_sub_context_t *ctx;
    int reenter;
} sink_t;
static void receive(void *user, const j1939_sub_update_t *u) {
    sink_t *sink=(sink_t *)user;
    assert(sink->count<128);
    sink->updates[sink->count++]=*u;
    if (sink->reenter) {
        uint32_t id=123;
        uint16_t n=123;
        j1939_sub_ref_t ref=u->selection;
        j1939_sub_frame_t f={0};
        assert(j1939_subscribe(sink->ctx,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_BUSY && id==123);
        assert(j1939_unsubscribe(sink->ctx,u->subscription_id)==J1939_SUB_BUSY);
        assert(j1939_sub_process(sink->ctx,&f,&n)==J1939_SUB_BUSY && n==123);
        assert(j1939_sub_select(sink->ctx,"subscriptions_synthetic.dbc","EngineA","Speed",&ref)==J1939_SUB_BUSY);
    }
}
static j1939_sub_ref_t select_signal(j1939_sub_context_t *c, const char *message, const char *signal) {
    j1939_sub_ref_t ref;
    assert(j1939_sub_select(c,"subscriptions_synthetic.dbc",message,signal,&ref)==0);
    return ref;
}
static uint32_t add(j1939_sub_context_t *c,const char *msg,const char *sig,int any,uint32_t tag) {
    uint32_t id=0;
    assert(j1939_subscribe(c,select_signal(c,msg,sig),any ? J1939_SUB_ANY_ROUTED_SOURCE : J1939_SUB_DECLARED_SOURCE,tag,&id)==0);
    assert(id); return id;
}
static unsigned send(j1939_sub_context_t *c,uint32_t id,int ext,const uint8_t *data,uint8_t size,
                      const j1939_sub_timestamp_t *stamp) {
    j1939_sub_frame_t f={0}; uint16_t n=999;
    f.can_id=id;f.extended=(uint8_t)ext;f.data=data;f.size=size;f.timestamp=stamp;
    assert(j1939_sub_process(c,&f,&n)==0);return n;
}
static void selection_and_storage(void) {
    j1939_sub_context_t c={0}; j1939_sub_slot_t slots[2];sink_t sink={0};
    j1939_sub_ref_t ref, unchanged; uint32_t a,b,id=987;
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,2,receive,&sink)==0);
    ref=select_signal(&c,"EngineAlias","Speed"); unchanged=ref;
    assert(j1939_sub_select(&c,"missing.dbc","EngineAlias","Speed",&ref)==J1939_SUB_NOT_FOUND);
    assert(ref.metadata==unchanged.metadata && ref.message_definition==unchanged.message_definition);
    assert(j1939_sub_select(&c,"subscriptions_synthetic.dbc","EngineAlias","missing",&ref)==J1939_SUB_NOT_FOUND);
    a=add(&c,"EngineA","Speed",0,10);b=add(&c,"Pressure","Pressure",0,20);
    assert(j1939_subscribe(&c,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_FULL && id==987);
    assert(j1939_unsubscribe(&c,a)==0 && j1939_unsubscribe(&c,a)==J1939_SUB_NOT_FOUND);
    assert(add(&c,"Counter","Count",0,30)>b);
    assert(j1939_unsubscribe(&c,0)==J1939_SUB_INVALID);
    ref.metadata=NULL;assert(j1939_subscribe(&c,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_INVALID);
    ref=select_signal(&c,"EngineA","Speed");ref.signal_definition=UINT32_MAX;
    assert(j1939_subscribe(&c,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_INVALID);
    assert(j1939_sub_init(&c,&subscriptions_metadata,NULL,0,receive,&sink)==0);
    ref=select_signal(&c,"EngineA","Speed");
    assert(j1939_subscribe(&c,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_FULL);
    puts("PASS selection, aliases, storage, stale tokens");
}
static void routing_values_and_mux(void) {
    j1939_sub_context_t c={0};j1939_sub_slot_t slots[12];sink_t sink={0};
    uint8_t data[8]={0x34,0x12,0xfe,1,25,255,0,0}, pressure[8]={0x01,0x02};
    uint32_t exact, duplicate;
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,12,receive,&sink)==0);
    exact=add(&c,"EngineA","Speed",0,10);duplicate=add(&c,"EngineA","Speed",0,11);
    add(&c,"EngineA","Speed",1,12);add(&c,"EngineAlias","Speed",0,13);
    add(&c,"EngineB","Speed",0,14);add(&c,"EngineA","Temperature",0,15);
    add(&c,"EngineA","Load",0,16);add(&c,"EngineA","Status",0,17);
    add(&c,"Pressure","Pressure",0,18);add(&c,"Counter","Count",0,19);
    assert(send(&c,0x18ff010b,1,data,8,NULL)==6);
    assert(sink.updates[0].subscription_id==exact && sink.updates[1].subscription_id==duplicate);
    assert(sink.updates[0].raw==4660 && sink.updates[0].engineering==582.5);
    assert(strcmp(sink.updates[0].unit,"rpm")==0 && sink.updates[0].pgn==0xff01 && sink.updates[0].source_address==11);
    assert(sink.updates[3].raw==254 && sink.updates[3].signed_raw==-2 && sink.updates[3].engineering==-41);
    assert(sink.updates[3].is_signed && strcmp(sink.updates[3].unit,"deg C")==0);
    assert(sink.updates[4].raw==25 && sink.updates[4].engineering==10);
    assert(sink.updates[5].quality==J1939_SUB_QUALITY_UNKNOWN && sink.updates[5].raw==255);
    assert(strcmp(sink.updates[5].unit,"")==0);
    assert(strcmp(j1939_metadata_text(&subscriptions_metadata,sink.updates[0].interpretation->scale_off),"0.125")==0);
    sink.count=0;assert(send(&c,0x18ff013e,1,data,8,NULL)==1);
    assert(sink.updates[0].tag==14 && sink.updates[0].engineering==1165);
    sink.count=0;assert(send(&c,0x18ff010c,1,data,8,NULL)==2);
    assert(sink.updates[0].tag==12 && sink.updates[1].tag==13);
    sink.count=0;assert(send(&c,0x0cff0101,1,data,8,NULL)==1 && sink.updates[0].tag==12);
    sink.count=0;data[3]=0;assert(send(&c,0x18ff010b,1,data,8,NULL)==5);
    assert(send(&c,0x18ff020b,1,pressure,8,NULL)==1);
    assert(sink.updates[5].raw==258 && sink.updates[5].engineering==511 && strcmp(sink.updates[5].unit,"kPa")==0);
    sink.count=0;assert(send(&c,100,0,data,1,NULL)==1);
    assert(sink.updates[0].raw==52 && sink.updates[0].pgn==0 && !sink.updates[0].is_signed);
    assert(j1939_unsubscribe(&c,duplicate)==0);sink.count=0;
    assert(send(&c,0x18ff010b,1,data,8,NULL)==4);
    puts("PASS multiple PGNs/sources/subscribers, numeric values, enums, mux, Motorola, unsubscribe");
}
static void timestamp_errors_and_reentrancy(void) {
    j1939_sub_context_t c={0};j1939_sub_slot_t slots[2];sink_t sink={0};
    j1939_sub_timestamp_t stamp={0,77};uint8_t data[8]={0};
    j1939_sub_frame_t f={0x18ff010b,data,&stamp,8,1};uint16_t count=999;
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,2,receive,&sink)==0);
    add(&c,"EngineA","Speed",0,0);sink.ctx=&c;sink.reenter=1;
    assert(j1939_sub_process(&c,&f,&count)==0 && count==1);
    assert(sink.updates[0].has_timestamp && sink.updates[0].timestamp.nanoseconds==0 && sink.updates[0].timestamp.clock_id==77);
    stamp.nanoseconds=UINT64_MAX;sink.reenter=0;assert(j1939_sub_process(&c,&f,&count)==0);
    assert(sink.updates[1].timestamp.nanoseconds==UINT64_MAX);
    f.timestamp=NULL;assert(j1939_sub_process(&c,&f,&count)==0 && !sink.updates[2].has_timestamp);
    sink.count=0;count=999;f.size=7;
    assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID && count==999 && sink.count==0);
    { const uint8_t short_payload[1]={0};
      f.data=short_payload;f.size=1;
      assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID && count==999 && sink.count==0); }
    f.size=8;f.data=NULL;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID);
    f.data=data;f.size=9;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_UNSUPPORTED);
    f.size=8;f.extended=2;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID);
    f.extended=1;f.can_id=0x98ff010b;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID);
    f.can_id=0x1aff010b;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_UNSUPPORTED);
    f.can_id=0x18fefe01;assert(j1939_sub_process(&c,&f,&count)==0 && count==0 && sink.count==0);
    f.can_id=0x800;f.extended=0;assert(j1939_sub_process(&c,&f,&count)==J1939_SUB_INVALID);
    assert(j1939_sub_process(&c,NULL,NULL)==J1939_SUB_INVALID);
    puts("PASS timestamps including valid zero, malformed/short/unknown frames, no partial delivery, reentrancy");
}
static void exact_wide_and_limits(void) {
    j1939_sub_context_t c={0};j1939_sub_slot_t slots[2];sink_t sink={0};
    uint8_t data[8]={255,255,255,255,255,255,255,255};uint32_t id=999;
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,2,receive,&sink)==0);
    add(&c,"WideUnsigned","Wide",0,1);add(&c,"WideSigned","Wide",0,2);
    assert(send(&c,0x18ff030b,1,data,8,NULL)==1);
    assert(sink.updates[0].raw==UINT64_MAX && sink.updates[0].engineering==0x1p64);
    assert(send(&c,0x18ff050b,1,data,8,NULL)==1 && sink.updates[1].signed_raw==-1);
    memset(data,0,8);data[7]=128;assert(send(&c,0x18ff050b,1,data,8,NULL)==1);
    assert(sink.updates[2].raw==(UINT64_C(1)<<63) && sink.updates[2].signed_raw==INT64_MIN);
    data[7]=127;memset(data,255,7);assert(send(&c,0x18ff050b,1,data,8,NULL)==1);
    assert(sink.updates[3].signed_raw==INT64_MAX && sink.updates[3].engineering==0x1p63);
    assert(j1939_unsubscribe(&c,1)==0);c.next_id=UINT32_MAX; /* white-box exhaustion boundary */
    id=add(&c,"Counter","Count",0,3);assert(id==UINT32_MAX);
    assert(j1939_unsubscribe(&c,id)==0);
    assert(j1939_subscribe(&c,select_signal(&c,"Counter","Count"),J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_ID_LIMIT);
    puts("PASS exact raw UINT64_MAX/INT64_MIN/MAX, double rounding, nonwrapping subscription IDs");
}
static void invalid_metadata(void) {
    j1939_sub_context_t c={0}; j1939_sub_slot_t slots[1];sink_t sink={0};
    j1939_db_metadata_t wrong=subscriptions_metadata;
    j1939_sub_ref_t ref={0};uint32_t id=123;
    assert(j1939_sub_init(NULL,&wrong,slots,1,receive,&sink)==J1939_SUB_INVALID);
    assert(j1939_sub_init(&c,&wrong,NULL,1,receive,&sink)==J1939_SUB_INVALID);
    wrong.version=99;assert(j1939_sub_init(&c,&wrong,slots,1,receive,&sink)==J1939_SUB_UNSUPPORTED && c.metadata==NULL);
    wrong=subscriptions_metadata;wrong.strings_size=0;
    assert(j1939_sub_init(&c,&wrong,slots,1,receive,&sink)==J1939_SUB_UNSUPPORTED);
    wrong=subscriptions_metadata;wrong.signals=NULL;
    assert(j1939_sub_init(&c,&wrong,slots,1,receive,&sink)==J1939_SUB_UNSUPPORTED);
    wrong=subscriptions_metadata;
    { j1939_signal_metadata_t bad=*wrong.signals;
      bad.unit_off=UINT32_MAX;wrong.signals=&bad;
      assert(j1939_sub_init(&c,&wrong,slots,1,receive,&sink)==J1939_SUB_UNSUPPORTED); }
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,1,receive,&sink)==0);
    ref=select_signal(&c,"Counter","Count");
    wrong=subscriptions_metadata;ref.metadata=&wrong;
    assert(j1939_subscribe(&c,ref,J1939_SUB_DECLARED_SOURCE,0,&id)==J1939_SUB_INVALID);
    puts("PASS unsupported metadata, wrong bundle, validation without allocation");
}
static void destination_and_overflow(void) {
    j1939_sub_context_t c={0}, second={0};j1939_sub_slot_t slots[2], other[1];sink_t sink={0}, other_sink={0};
    uint8_t data[8]={2,0,0,0,0,0,0,0};
    assert(j1939_sub_init(&c,&subscriptions_metadata,slots,2,receive,&sink)==0);
    assert(j1939_sub_init(&second,&subscriptions_metadata,other,1,receive,&other_sink)==0);
    add(&c,"Destination","Value",0,1);add(&c,"Overflow","Value",0,2);
    add(&second,"Destination","Value",0,3);
    assert(send(&c,0x18ef990b,1,data,8,NULL)==1); /* same PDU1 PGN, different DA */
    assert(sink.updates[0].pgn==0xef00 && sink.updates[0].raw==2 && sink.updates[0].engineering_finite);
    assert(send(&c,0x18ef990c,1,data,8,NULL)==0); /* exact source default */
    assert(send(&c,0x18ff060b,1,data,8,NULL)==1);
    assert(sink.updates[1].raw==2 && !sink.updates[1].engineering_finite && isinf(sink.updates[1].engineering));
    assert(sink.updates[1].quality==J1939_SUB_QUALITY_UNKNOWN);
    assert(other_sink.count==0 && send(&second,0x18ef990b,1,data,8,NULL)==1 && other_sink.count==1);
    puts("PASS PDU1 destination normalization, engineering overflow, independent contexts");
}
int main(void) {
    selection_and_storage();routing_values_and_mux();timestamp_errors_and_reentrancy();
    exact_wide_and_limits();invalid_metadata();destination_and_overflow();
    puts("6 subscription scenario groups passed");return 0;
}
