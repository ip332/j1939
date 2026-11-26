/* Host gateway: one metadata blob per queue generation, no descriptor per record. */
#include "signal_keys.h"
#include "j1939_key_queue.h"
#include "j1939_latest.h"
#include <inttypes.h>
#include <float.h>
#include <stdio.h>
static const uint8_t expected_catalog[32]=SUB_GENERATED_CATALOG_SHA256;
typedef struct { j1939_key_context_t *context; j1939_key_queue_t *queue; j1939_latest_t *latest; int error; } application_t;
static void json_string(const char *s) {
    const unsigned char *p=(const unsigned char *)s;putchar('"');
    for(;*p;++p){if(*p=='"' || *p=='\\'){putchar('\\');putchar(*p);}else if(*p<32)printf("\\u%04x",(unsigned)*p);else putchar(*p);}putchar('"');
}
static void receive(void *user,const j1939_key_update_t *u) {
    application_t *app=(application_t *)user;
    app->error=j1939_key_queue_push(app->queue,app->context,u);
    if(!app->error)app->error=j1939_latest_put(app->latest,app->context,u);
}
static void metadata_header(const j1939_key_queue_t *q) {
    const j1939_key_identity_t *id=&q->metadata->identity;unsigned i;
    printf("{\"type\":\"metadata\",\"generation\":\"%" PRIu64 "\",\"catalog_sha256\":\"",q->generation);
    for(i=0;i<32;++i)printf("%02x",id->catalog_sha256[i]);
    printf("\",\"layout\":[%u,%u,%u],\"layout_version\":%" PRIu32 ",\"source_mode\":%u,\"source_namespace\":\"%" PRIu64 "\",\"mapping_hash\":\"%016" PRIx64 "\"}\n",
        id->layout.domain_bits,id->layout.source_bits,id->layout.signal_bits,id->version,id->source_mode,id->source_namespace,id->mapping_hash);
}
static int drain(j1939_key_queue_t *q) {
    j1939_key_record_t r;uint64_t generation;int status;
    while((status=j1939_key_queue_pop(q,&r,&generation))==0) {
        j1939_sub_ref_t ref;const j1939_signal_t *signal;uint16_t index;
        uint32_t domain,source,id,pgn;const j1939_db_metadata_t *m;double engineering;
        if(j1939_key_queue_descriptor(q,generation,&r,&ref))return 1;
        m=ref.metadata;index=m->signal_definitions[ref.signal_definition].signal_index;signal=&m->db->signals[index];
        engineering=j1939_signal_phys(signal,r.raw);
        (void)j1939_key_unpack(q->metadata->identity.layout,r.packed_key,&domain,&source,&id);
        j1939_id_unpack(r.can_id,1,&pgn,NULL,NULL,NULL);
        printf("{\"type\":\"observation\",\"subscription\":%" PRIu32 ",\"tag\":%" PRIu32 ",\"signal\":",r.subscription_id,r.tag);
        json_string(m->db->strings+m->signal_definitions[ref.signal_definition].name_off);
        printf(",\"raw\":\"%" PRIu64 "\",\"engineering\":",r.raw);
        if(engineering<=DBL_MAX && engineering>=-DBL_MAX)printf("%.17g",engineering);else printf("null");
        printf(",\"unit\":");
        json_string(j1939_signal_unit(m,index));
        printf(",\"source\":%" PRIu32 ",\"source_identity\":%" PRIu32 ",\"pgn\":%" PRIu32 ",\"quality\":\"unknown\",\"timestamp_ns\":",r.can_id&255u,source,pgn);
        if(r.has_timestamp)printf("\"%" PRIu64 "\",\"clock_id\":%" PRIu32,r.nanoseconds,r.clock_id);else printf("null,\"clock_id\":null");
        printf(",\"packed_key\":%" PRIu32 ",\"signal_id\":%" PRIu32 ",\"domain\":%" PRIu32 "}\n",r.packed_key,id,domain);
    }
    return status==J1939_SUB_NOT_FOUND?0:2;
}
/* Sketch of a generated typed convenience wrapper: fixed ID, caller selects source. */
static int j1939_get_EngineSpeed(const j1939_latest_t *store,const j1939_key_context_t *active,
    uint32_t domain,uint32_t source,const j1939_latest_freshness_t *policy,j1939_latest_value_t *out) {
    return j1939_latest_get_with_policy(store,active,&active->identity,
        SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED,domain,source,policy,out);
}
int main(void) {
    j1939_key_context_t context;j1939_sub_slot_t base[4];j1939_key_slot_t filters[4];
    j1939_key_queue_metadata_t blob;j1939_key_queue_t queue;j1939_key_record_t records[8];
    j1939_latest_t latest;j1939_latest_slot_t retained[4];
    application_t app={&context,&queue,&latest,0};j1939_key_layout_t layout=SUB_GENERATED_LAYOUT;
    const uint32_t ids[]={SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD,
        SUB_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_SPEED,SUB_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE};
    unsigned i;uint8_t engine[8]={0x40,0x1f,0,1,125,0,0,0},pressure[8]={0,50,0,0,0,0,0,0};
    j1939_sub_timestamp_t ts={1000000000,7};j1939_sub_frame_t f={0x18ff010b,engine,&ts,8,1};
    if(j1939_keys_init(&context,&generated_keys_catalog,expected_catalog,layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,filters,4,receive,&app))return 1;
    if(j1939_key_queue_metadata_init(&blob,&context) || j1939_key_queue_init(&queue,&blob,records,8))return 2;
    if(j1939_latest_init(&latest,&context,retained,4))return 8;
    for(i=0;i<4;++i){uint32_t key,token;
        if(j1939_key_pack(layout,1,i==2?62:11,ids[i],&key) || j1939_keys_subscribe_key(&context,&context.identity,key,i,&token))return 3;}
    if(j1939_keys_process(&context,1,&f,NULL) || app.error)return 4;
    f.can_id=0x18ff013e;f.timestamp=NULL;if(j1939_keys_process(&context,1,&f,NULL) || app.error)return 5;
    f.can_id=0x18ff020b;f.data=pressure;if(j1939_keys_process(&context,1,&f,NULL) || app.error)return 6;
    if(queue.count!=4 || queue.dropped)return 7;
    metadata_header(&queue);if(drain(&queue))return 9;
    /* App selects optional policy. Checked reads reject missing time, rather than
     * showing untimestamped data as fresh. NULL permits app-managed freshness. */
    for(i=0;i<4;++i) {
        j1939_latest_value_t value;
        const j1939_latest_freshness_t policy={{1000000100,7},500000000};
        int status=i==0?j1939_get_EngineSpeed(&latest,&context,1,11,&policy,&value):
            j1939_latest_get_with_policy(&latest,&context,&context.identity,ids[i],1,i==2?62:11,&policy,&value);
        printf("{\"type\":\"latest\",\"signal_id\":%" PRIu32 ",\"status\":%d",ids[i],status);
        if(status==0)printf(",\"raw\":\"%" PRIu64 "\",\"engineering\":%.17g",value.raw,value.engineering);
        /* No value fields are printed for errors. Caller can display unavailable
         * or retain a separate historical display, without presenting it as fresh. */
        puts("}");
    }
    {j1939_latest_value_t value;int status=j1939_get_EngineSpeed(&latest,&context,1,11,NULL,&value);
     if(status || value.raw!=8000 || value.engineering!=1000)return 10;}
    return 0;
}
