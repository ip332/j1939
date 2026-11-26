#include "signal_keys.h"
static const uint8_t expected_catalog[32]=SUB_GENERATED_CATALOG_SHA256;
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define A SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED
#define B SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_SPEED
#define LOAD SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD
#define TEMP SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE
typedef struct { j1939_key_update_t updates[64];unsigned count; j1939_key_context_t *ctx; int reenter; } sink_t;
static void receive(void *user,const j1939_key_update_t *u) {
    sink_t *s=(sink_t *)user;assert(s->count<64);s->updates[s->count++]=*u;
    if(s->reenter){uint32_t token=777;uint16_t n=777;j1939_sub_ref_t ref={0};j1939_sub_frame_t f={0};j1939_key_filter_t filter={0};
        assert(j1939_keys_subscribe(s->ctx,&s->ctx->identity,A,filter,0,&token)==J1939_SUB_BUSY && token==777);
        assert(j1939_keys_unsubscribe(s->ctx,u->value.subscription_id)==J1939_SUB_BUSY);
        assert(j1939_keys_process(s->ctx,0,&f,&n)==J1939_SUB_BUSY && n==777);
        assert(j1939_keys_decode(s->ctx,&s->ctx->identity,u->packed_key,&ref)==J1939_SUB_BUSY);
    }
}
static void bit_layouts(void) {
    const j1939_key_layout_t layouts[]={{2,10,20},{8,8,16},{1,1,30},{30,1,1}};unsigned i;
    for(i=0;i<4;++i){uint32_t k=0,d,s,id; j1939_key_layout_t l=layouts[i];
        uint32_t dm=UINT32_MAX>>(32-l.domain_bits),sm=UINT32_MAX>>(32-l.source_bits),im=UINT32_MAX>>(32-l.signal_bits);
        assert(j1939_key_pack(l,dm,sm,im,&k)==0 && k==UINT32_MAX);
        assert(j1939_key_unpack(l,k,&d,&s,&id)==0 && d==dm && s==sm && id==im);
        assert(j1939_key_pack(l,0,0,0,&k)==0 && k==0);
        assert(j1939_key_pack(l,dm+1,0,0,&k)==J1939_SUB_INVALID && k==0);
        assert(j1939_key_pack(l,0,sm+1,0,&k)==J1939_SUB_INVALID && k==0);
        assert(j1939_key_pack(l,0,0,im+1,&k)==J1939_SUB_INVALID && k==0);
    }
    {const j1939_key_layout_t bad[]={{0,8,24},{8,0,24},{8,8,0},{8,8,15},{8,8,17},{33,1,1},{255,1,1},{32,0,0}};
     for(i=0;i<8;++i){uint32_t k=123,d=123,s=123,id=123;
        assert(!j1939_key_layout_valid(bad[i]));assert(j1939_key_pack(bad[i],0,0,0,&k)==J1939_SUB_INVALID && k==123);
        assert(j1939_key_unpack(bad[i],0,&d,&s,&id)==J1939_SUB_INVALID && d==123 && s==123 && id==123);}}
    {j1939_key_layout_t out={8,8,16};assert(j1939_key_layout_make(2,10,20,&out)==0 && out.signal_bits==20);
     assert(j1939_key_layout_make(257,8,23,&out)==J1939_SUB_INVALID && out.domain_bits==2);
     assert(j1939_key_layout_make(UINT32_MAX,1,1,&out)==J1939_SUB_INVALID);}
    puts("PASS configurable layouts, zero/max keys, boundaries, overflow and invalid widths");
}
static void source_addresses(void) {
    j1939_key_context_t c={0};j1939_sub_slot_t base[8];j1939_key_slot_t keys[8];sink_t sink={0};
    j1939_key_layout_t layout={8,8,16};j1939_key_filter_t exact={1,11,0,0},wild={0,0,1,1};
    uint8_t bytes[8]={0x34,0x12,0xfe,1,25,255,0,0};j1939_sub_timestamp_t ts={0,7};
    j1939_sub_frame_t frame={0x18ff010b,bytes,&ts,8,1};uint32_t token,first;uint16_t n; j1939_sub_ref_t ref;
    assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,keys,8,receive,&sink)==0);
    sink.ctx=&c;sink.reenter=1;
    assert(j1939_keys_subscribe(&c,&c.identity,A,exact,1,&first)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,A,wild,2,&token)==0);
    exact.source=62;assert(j1939_keys_subscribe(&c,&c.identity,B,exact,3,&token)==0);
    exact.source=11;assert(j1939_keys_subscribe(&c,&c.identity,LOAD,exact,4,&token)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,TEMP,exact,5,&token)==0);
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==4);
    assert(sink.updates[0].value.raw==4660 && sink.updates[0].value.engineering==582.5);
    assert(sink.updates[0].value.timestamp.nanoseconds==0 && sink.updates[0].value.has_timestamp);
    assert(strcmp(sink.updates[0].value.unit,"rpm")==0 && sink.updates[2].value.engineering==10);
    assert(sink.updates[3].value.signed_raw==-2 && sink.updates[3].value.engineering==-41);
    assert(sink.updates[0].source_identity==11 && sink.updates[0].domain==1 && sink.updates[0].signal_id==A);
    assert(j1939_keys_decode(&c,&c.identity,sink.updates[0].packed_key,&ref)==0);
    assert(ref.signal_definition==generated_keys_catalog.entries[A].signal_definition);
    sink.reenter=0;sink.count=0;frame.can_id=0x18ff013e;
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[0].signal_id==B && sink.updates[0].value.engineering==1165);
    frame.can_id=0x18ff010b;assert(j1939_keys_process(&c,2,&frame,&n)==0 && n==1 && sink.updates[1].value.tag==2);
    bytes[3]=0;assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==3);
    assert(j1939_keys_unsubscribe(&c,first)==0 && j1939_keys_unsubscribe(&c,first)==J1939_SUB_NOT_FOUND);
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==2);
    {j1939_key_filter_t impossible={1,11,0,0};assert(j1939_keys_subscribe(&c,&c.identity,B,impossible,0,&token)==J1939_SUB_INVALID);}
    {j1939_key_filter_t boundary={255,255,0,0};assert(j1939_keys_subscribe(&c,&c.identity,A,boundary,9,&token)==0);
     frame.can_id=0x18ff01ff;sink.count=0;assert(j1939_keys_process(&c,255,&frame,&n)==0 && n==2);
     assert(sink.updates[1].domain==255 && sink.updates[1].source_identity==255);}
    assert(j1939_keys_subscribe(&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDEUNSIGNED_WIDE,exact,10,&token)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_WIDESIGNED_WIDE,exact,11,&token)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_OVERFLOW_VALUE,exact,12,&token)==0);
    memset(bytes,255,8);frame.can_id=0x18ff030b;sink.count=0;
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[0].value.raw==UINT64_MAX);
    frame.can_id=0x18ff050b;memset(bytes,0,8);bytes[7]=128;
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[1].value.signed_raw==INT64_MIN);
    frame.can_id=0x18ff060b;bytes[0]=2;
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && !sink.updates[2].value.engineering_finite);
    assert(sink.updates[2].value.quality==J1939_SUB_QUALITY_UNKNOWN);
    puts("PASS exact/wildcard domains and sources, overrides, mux, values, timestamps, unsubscribe, reentrancy");
}
static void logical_addresses(void) {
    j1939_key_context_t c={0};j1939_sub_slot_t base[2];j1939_key_slot_t slots[2];sink_t sink={0};
    j1939_key_layout_t layout={2,10,20};j1939_key_filter_t f={1,700,0,0};
    const j1939_key_mapping_t map[]={{1,700,11},{1,800,62}}, moved[]={{1,700,12},{1,800,62}};
    uint8_t bytes[8]={8};j1939_sub_frame_t frame={0x18ff010b,bytes,NULL,8,1};uint32_t token,oldkey;uint16_t n=123;
    j1939_key_identity_t old_identity; j1939_sub_ref_t ref={0};
    assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,123,map,2,base,slots,2,receive,&sink)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,A,f,1,&token)==0);
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[0].source_identity==700);
    old_identity=c.identity;oldkey=sink.updates[0].packed_key;
    frame.can_id=0x18ff010c;assert(j1939_keys_process(&c,1,&frame,&n)==J1939_KEY_MAPPING_MISSING);
    assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,123,moved,2,base,slots,2,receive,&sink)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,A,f,1,&token)==0);
    assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[1].packed_key==oldkey);
    assert(old_identity.mapping_hash!=c.identity.mapping_hash);
    assert(j1939_keys_decode(&c,&old_identity,oldkey,&ref)==J1939_KEY_MISMATCH);
    assert(j1939_keys_decode(&c,&c.identity,oldkey,&ref)==0);
    frame.can_id=0x18ff010b;n=123;assert(j1939_keys_process(&c,1,&frame,&n)==J1939_KEY_MAPPING_MISSING && n==123);
    f.source=999;assert(j1939_keys_subscribe(&c,&c.identity,A,f,0,&token)==J1939_KEY_MAPPING_MISSING);
    {const j1939_key_mapping_t maxmap[]={{3,1023,255}};f.domain=3;f.source=1023;
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,123,maxmap,1,base,slots,2,receive,&sink)==0);
     assert(j1939_keys_subscribe(&c,&c.identity,A,f,0,&token)==0);
     frame.can_id=0x18ff01ff;assert(j1939_keys_process(&c,3,&frame,&n)==0 && n==1 && sink.updates[2].source_identity==1023);}
    {const j1939_key_mapping_t moved_override[]={{1,700,12},{1,800,14}};j1939_key_filter_t source_b={1,800,0,0};
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,123,moved_override,2,base,slots,2,receive,&sink)==0);
     assert(j1939_keys_subscribe(&c,&c.identity,B,source_b,0,&token)==J1939_SUB_INVALID);}
    {const j1939_key_mapping_t domains[]={{1,700,11},{1,800,62},{2,700,11}};
     j1939_sub_slot_t base3[3];j1939_key_slot_t slots3[3];j1939_key_filter_t w={0,700,1,0},w2={1,0,0,1},both={0,0,1,1};
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,123,domains,3,base3,slots3,3,receive,&sink)==0);
     assert(j1939_keys_subscribe(&c,&c.identity,A,w,1,&token)==0);
     assert(j1939_keys_subscribe(&c,&c.identity,A,w2,2,&token)==0);
     assert(j1939_keys_subscribe(&c,&c.identity,B,both,3,&token)==0);
     frame.can_id=0x18ff010b;sink.count=0;assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==2);
     assert(j1939_keys_process(&c,2,&frame,&n)==0 && n==1 && sink.updates[2].source_identity==700);
     frame.can_id=0x18ff013e;assert(j1939_keys_process(&c,1,&frame,&n)==0 && n==1 && sink.updates[3].signal_id==B);}
    puts("PASS logical ECU mapping, address moves, unchanged key/new binding, missing mapping, 10-bit ECU boundary");
}
static void rejects_and_spns(void) {
    j1939_key_context_t c={0};j1939_sub_slot_t base[1];j1939_key_slot_t slots[1];sink_t sink={0};
    j1939_key_layout_t layout={2,10,20};j1939_key_filter_t f={1,11,0,0};uint32_t token=999,ids[3],key;
    j1939_key_identity_t wrong;j1939_sub_ref_t ref={0};
    assert(j1939_keys_by_spn(&generated_keys_catalog,100,ids,3)==3 && ids[0]!=ids[1] && ids[1]!=ids[2]);
    assert(j1939_keys_by_spn(&generated_keys_catalog,100,NULL,0)==3);
    assert(j1939_keys_by_spn(&generated_keys_catalog,0,ids,3)==1 && ids[0]==SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE);
    assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==0);
    {uint8_t wrong_header[32];unsigned i;for(i=0;i<32;++i)wrong_header[i]=expected_catalog[i];wrong_header[0]^=1;
     assert(j1939_keys_init(&c,&generated_keys_catalog,wrong_header,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_KEY_MISMATCH);}
    wrong=c.identity;wrong.catalog_sha256[0]^=1;
    assert(j1939_keys_subscribe(&c,&wrong,A,f,0,&token)==J1939_KEY_MISMATCH && token==999);
    assert(j1939_keys_decode(&c,&wrong,UINT32_MAX,&ref)==J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.layout=(j1939_key_layout_t){8,8,16};assert(j1939_keys_decode(&c,&wrong,0,&ref)==J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.version=2;assert(j1939_keys_decode(&c,&wrong,0,&ref)==J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.source_mode=1;assert(j1939_keys_decode(&c,&wrong,0,&ref)==J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.source_namespace=1;assert(j1939_keys_decode(&c,&wrong,0,&ref)==J1939_KEY_MISMATCH);
    wrong=c.identity;wrong.mapping_hash^=1;assert(j1939_keys_decode(&c,&wrong,0,&ref)==J1939_KEY_MISMATCH);
    f.source=1000;assert(j1939_keys_subscribe(&c,&c.identity,A,f,0,&token)==J1939_SUB_INVALID);
    assert(j1939_key_pack(layout,1,1000,A,&key)==0 && j1939_keys_decode(&c,&c.identity,key,&ref)==J1939_SUB_INVALID);
    f.source=11;
    {j1939_key_filter_t invalid={1,0,1,1};assert(j1939_keys_subscribe(&c,&c.identity,A,invalid,0,&token)==J1939_SUB_INVALID);}
    assert(j1939_keys_subscribe(&c,&c.identity,A,f,0,&token)==0);
    assert(j1939_keys_subscribe(&c,&c.identity,A,f,0,&token)==J1939_SUB_FULL);
    {const uint8_t short_data[1]={0};j1939_sub_frame_t frame={0x18ff010b,short_data,NULL,1,1};uint16_t n=999;
     assert(j1939_keys_process(&c,1,&frame,&n)==J1939_SUB_INVALID && n==999 && sink.count==0);
     frame.size=9;assert(j1939_keys_process(&c,1,&frame,&n)==J1939_SUB_UNSUPPORTED);
     frame.size=1;frame.data=NULL;assert(j1939_keys_process(&c,1,&frame,&n)==J1939_SUB_INVALID);}
    {j1939_key_catalog_t bad=generated_keys_catalog;bad.metadata_hash^=1;
     assert(j1939_keys_init(&c,&bad,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_KEY_MISMATCH);
     assert(base[0].id); /* failed init did not clear active storage */
     bad=generated_keys_catalog;bad.entry_hash^=1;assert(j1939_keys_init(&c,&bad,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_KEY_MISMATCH);}
    {j1939_key_catalog_t bad=generated_keys_catalog;j1939_key_entry_t entries[64];uint32_t i;
     assert(bad.count<=64);for(i=0;i<bad.count;++i)entries[i]=bad.entries[i];
     entries[0]=bad.entries[1];entries[1]=bad.entries[0];bad.entries=entries;
     assert(j1939_keys_init(&c,&bad,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_KEY_MISMATCH);}
    {j1939_key_catalog_t bad=generated_keys_catalog;j1939_db_metadata_t meta=*bad.metadata;j1939_db_t db=*meta.db;
     j1939_signal_t signals[64];uint32_t i;assert(db.signal_count<=64);
     for(i=0;i<db.signal_count;++i){signals[i]=db.signals[i];}
     signals[0].scale+=1;db.signals=signals;meta.db=&db;bad.metadata=&meta;
     assert(j1939_keys_init(&c,&bad,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_KEY_MISMATCH);}
    {const j1939_key_mapping_t duplicate[]={{1,10,11},{1,20,11}}, overflow[]={{4,0,11}}, idoverflow[]={{1,1024,11}};
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,1,duplicate,2,base,slots,1,receive,&sink)==J1939_KEY_AMBIGUOUS);
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,1,overflow,1,base,slots,1,receive,&sink)==J1939_SUB_INVALID);
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,1,idoverflow,1,base,slots,1,receive,&sink)==J1939_SUB_INVALID);}
    {const j1939_key_mapping_t bad_address[]={{1,700,300}};
     assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_LOGICAL_ECU,1,bad_address,1,base,slots,1,receive,&sink)==J1939_SUB_INVALID);}
    layout=(j1939_key_layout_t){15,16,1};assert(j1939_keys_init(&c,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,&sink)==J1939_SUB_ID_LIMIT);
    puts("PASS SPN duplicates/absence/zero, identity and layout mismatch, exhaustion, mapping errors, transactional init");
}
int main(void) {bit_layouts();source_addresses();logical_addresses();rejects_and_spns();puts("4 packed-key scenario groups passed");return 0;}
