/* Host JSON adapter around allocation-free C subscription/queue/latest modules. */
#include "live_a_keys.h"
#include "live_b_keys.h"
#include "j1939_latest.h"
#include "frames.h"
#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#define N 9
#define Q 16
static const uint8_t stamps[2][32]={LIVE_A_KEYS_CATALOG_SHA256,LIVE_B_KEYS_CATALOG_SHA256};
static const uint32_t ids[2][N]={
 {LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED,LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE,
  LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_MODE,LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD,
  LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_STATUS,LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_SPEED,
  LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_TEMPERATURE,LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,
  LIVE_A_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_WIDEUNSIGNED_WIDE},
 {LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_SPEED,LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_TEMPERATURE,
  LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_MODE,LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_LOAD,
  LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEA_STATUS,LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_SPEED,
  LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_ENGINEB_TEMPERATURE,LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_PRESSURE_PRESSURE,
  LIVE_B_KEYS_ID_SUBSCRIPTIONS_SYNTHETIC_WIDEUNSIGNED_WIDE}};
static const uint32_t sources[N]={11,11,11,11,11,62,62,11,11};
typedef struct {j1939_key_context_t *ctx;j1939_key_queue_t *queue;j1939_latest_t *latest;unsigned full;} app_t;
static void text(const char *s){const unsigned char *p=(const unsigned char *)s;putchar('"');for(;*p;++p){if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}else if(*p<32)printf("\\u%04x",(unsigned)*p);else putchar(*p);}putchar('"');}
static void identity(const j1939_key_queue_t *q){unsigned i;printf("\"generation\":\"%" PRIu64 "\",\"catalog\":\"",q->generation);for(i=0;i<32;++i)printf("%02x",q->metadata->identity.catalog_sha256[i]);putchar('"');}
static void status(const char *name,int code){printf("{\"type\":\"status\",\"name\":");text(name);printf(",\"status\":%d}\n",code);}
static void binding(const j1939_key_queue_t *q){const j1939_key_identity_t *id=&q->metadata->identity;printf("{\"type\":\"activate\",");identity(q);printf(",\"layout\":[%u,%u,%u],\"version\":%" PRIu32 ",\"source_mode\":%u,\"namespace\":\"%" PRIu64 "\",\"mapping\":\"%016" PRIx64 "\"}\n",id->layout.domain_bits,id->layout.source_bits,id->layout.signal_bits,id->version,id->source_mode,id->source_namespace,id->mapping_hash);}
static void receive(void *user,const j1939_key_update_t *u){
 app_t *a=user;int result=j1939_key_queue_push(a->queue,a->ctx,u);
 if(result==J1939_SUB_FULL)++a->full;else assert(result==0);
 /* Independent stores: a full delivery queue does not stop latest retention. */
 assert(j1939_latest_put(a->latest,a->ctx,u)==0);
}
static void consume(void *user,j1939_key_queue_t *q,const j1939_key_record_t *r,uint64_t epoch){
 j1939_sub_ref_t ref;const j1939_signal_t *s;uint16_t si;uint32_t d,sa,id,pgn;uint64_t mask;int64_t signed_raw=0;double value;(void)user;
 assert(j1939_key_queue_descriptor(q,epoch,r,&ref)==0);
 si=ref.metadata->signal_definitions[ref.signal_definition].signal_index;s=&ref.metadata->db->signals[si];
 assert(j1939_key_unpack(q->metadata->identity.layout,r->packed_key,&d,&sa,&id)==0);
 mask=s->length==64?UINT64_MAX:(UINT64_C(1)<<s->length)-1;
 if(s->flags & J1939_SIG_SIGNED)signed_raw=(r->raw & (UINT64_C(1)<<(s->length-1)))?-1-(int64_t)(~r->raw & mask):(int64_t)r->raw;
 value=j1939_signal_phys(s,r->raw);j1939_id_unpack(r->can_id,1,&pgn,NULL,NULL,NULL);
 printf("{\"type\":\"observation\",");identity(q);
 printf(",\"key\":%" PRIu32 ",\"signal_id\":%" PRIu32 ",\"domain\":%" PRIu32 ",\"source\":%" PRIu32 ",\"pgn\":%" PRIu32 ",\"raw\":\"%" PRIu64 "\",\"signed_raw\":",r->packed_key,id,d,sa,pgn,r->raw);
 if(s->flags & J1939_SIG_SIGNED)printf("\"%" PRId64 "\"",signed_raw);else printf("null");
 printf(",\"engineering\":");if(value<=DBL_MAX && value>=-DBL_MAX)printf("%.17g",value);else printf("null");
 printf(",\"unit\":");text(j1939_signal_unit(ref.metadata,si));
 printf(",\"timestamp_ns\":\"%" PRIu64 "\",\"clock_id\":%" PRIu32 ",\"quality\":\"unknown\"}\n",r->nanoseconds,r->clock_id);
}
static void clock_event(uint64_t now){printf("{\"type\":\"clock\",\"now_ns\":\"%" PRIu64 "\",\"clock_id\":7,\"max_age_ns\":\"1000000000\"}\n",now);}
int main(void){
 j1939_key_context_t ctx[2];j1939_sub_slot_t slots[2][N];j1939_key_slot_t filters[2][N];
 j1939_key_queue_t q[2];j1939_key_queue_metadata_t metadata[2];j1939_key_record_t records[2][Q];
 j1939_latest_t latest[2];j1939_latest_slot_t retained[2][N];j1939_key_pair_t pair;
 app_t apps[2];const j1939_key_catalog_t *catalogs[]={&live_a_keys_catalog,&live_b_keys_catalog};
 j1939_key_layout_t layout={8,8,16};unsigned b,i;uint16_t n;int result;j1939_latest_value_t value;
 for(b=0;b<2;++b){apps[b]=(app_t){&ctx[b],&q[b],&latest[b],0};
  assert(j1939_keys_init(&ctx[b],catalogs[b],stamps[b],layout,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,slots[b],filters[b],N,receive,&apps[b])==0);
  assert(j1939_key_queue_metadata_init(&metadata[b],&ctx[b])==0 && j1939_key_queue_init(&q[b],&metadata[b],records[b],Q)==0);
  assert(j1939_latest_init(&latest[b],&ctx[b],retained[b],N)==0);
  for(i=0;i<N;++i){uint32_t token;j1939_key_filter_t f={1,sources[i],0,0};assert(ids[0][i]==ids[1][i]);assert(j1939_keys_subscribe(&ctx[b],&ctx[b].identity,ids[b][i],f,i,&token)==0);}}
 assert(j1939_key_pair_init(&pair,&q[0],&ctx[0],&q[1],&ctx[1])==0);clock_event(UINT64_C(1400000000));binding(&q[0]);
 for(i=0;i<FRAME_COUNT;++i)assert(j1939_key_pair_process(&pair,1,&demo_frames[i],NULL)==0);
 assert(q[0].count==13 && q[0].dropped==0); /* mux-off frame emits four, not five */
 assert(j1939_key_pair_switch(&pair)==0);binding(&q[1]);
 result=j1939_latest_get(&latest[0],&ctx[1],&ctx[1].identity,ids[1][0],1,11,&value);assert(result==J1939_KEY_MISMATCH);status("old-latest/wrong-catalog",result);
 for(i=0;i<4;++i)assert(j1939_key_pair_process(&pair,1,&demo_frames[i],NULL)==0);
 result=j1939_key_pair_switch(&pair);assert(result==J1939_SUB_BUSY);status("third-switch/bank-unavailable",result);
 assert(j1939_key_pair_drain(&pair,1,consume,NULL,&n)==0 && n==1 && q[0].count==12);
 assert(j1939_key_pair_switch(&pair)==J1939_SUB_BUSY);
 assert(j1939_key_pair_drain(&pair,UINT16_MAX,consume,NULL,&n)==0 && n==21);
 assert(!q[0].count && !q[1].count && !apps[0].full && !apps[1].full);status("normal-transition-drops",0);
 clock_event(UINT64_C(1500000000));
 {j1939_latest_freshness_t policy={{1500000000,7},1000000000};
  result=j1939_latest_get_with_policy(&latest[1],&ctx[1],&ctx[1].identity,ids[1][0],1,11,&policy,&value);
  assert(result==0 && value.raw==8000 && value.engineering==2000);status("latest-fresh",result);
  policy.now.nanoseconds=3000000001ULL;result=j1939_latest_get_with_policy(&latest[1],&ctx[1],&ctx[1].identity,ids[1][0],1,11,&policy,&value);
  assert(result==J1939_LATEST_STALE);clock_event(policy.now.nanoseconds);status("latest-stale",result);}
 /* Separate deliberate overload after the loss-free handoff; no unbounded claim. */
 for(i=0;i<4;++i)assert(j1939_key_pair_process(&pair,1,&demo_frames[0],NULL)==0);
 assert(q[1].count==16 && q[1].dropped==4 && apps[1].full==4);status("overload-drop-newest",(int)q[1].dropped);
 assert(j1939_key_pair_drain(&pair,UINT16_MAX,consume,NULL,&n)==0 && n==16);
 printf("{\"type\":\"footprint\",\"context_bytes\":%zu,\"slot_bytes\":%zu,\"queue_record_bytes\":%zu,\"latest_slot_bytes\":%zu,\"caller_ram_bytes\":%zu}\n",sizeof(ctx[0]),sizeof(slots[0][0])+sizeof(filters[0][0]),sizeof(records[0][0]),sizeof(retained[0][0]),sizeof(ctx)+sizeof(slots)+sizeof(filters)+sizeof(q)+sizeof(metadata)+sizeof(records)+sizeof(latest)+sizeof(retained)+sizeof(pair)+sizeof(apps));
 puts("{\"type\":\"done\"}");return 0;
}
