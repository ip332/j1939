/* Host-only bridge: inspect actual generated C tables, never parse DBC text. */
#include "j1939_signal_keys.h"
#include <inttypes.h>
#include <stdio.h>
#ifndef METADATA_SYMBOL
#error supply generated metadata symbol
#endif
extern const j1939_db_metadata_t METADATA_SYMBOL;
static void string(const char *s) {
    const unsigned char *p=(const unsigned char *)s;putchar('"');
    for(;*p;++p){if(*p=='"' || *p=='\\'){putchar('\\');putchar(*p);}else if(*p<32)printf("\\u%04x",(unsigned)*p);else putchar(*p);}putchar('"');
}
static void optional(uint32_t value,int present) { if(present)printf("%" PRIu32,value);else printf("null"); }
int main(void) {
    const j1939_db_metadata_t *m=&METADATA_SYMBOL;uint32_t i,j,n=0;
    printf("{\"metadata_hash\":\"%016" PRIx64 "\",\"entries\":[",j1939_key_metadata_hash(m));
    for(i=0;i<m->message_definition_count;++i) {
        const j1939_message_definition_t *d=&m->message_definitions[i];
        const j1939_message_t *msg=&m->db->messages[d->message_index];
        const j1939_source_metadata_t *source=&m->sources[d->source_index];
        for(j=0;j<d->signal_count;++j) {
            uint32_t k=d->signal_begin+j,x;
            const j1939_signal_definition_t *def=&m->signal_definitions[k];
            const j1939_signal_t *s=&m->db->signals[def->signal_index];
            const j1939_signal_metadata_t *meta=&m->signals[def->signal_index];
            const uint32_t literals[4]={meta->scale_off,meta->offset_off,meta->minimum_off,meta->maximum_off};
            const double values[4]={s->scale,s->offset,s->min_value,s->max_value};
            printf("%s{\"source\":\"%s\",\"dbc_id\":%" PRIu32 ",\"message\":\"%s\",\"signal\":\"%s\",\"message_definition\":%" PRIu32 ",\"signal_definition\":%" PRIu32 ",\"semantic\":{",n++?",":"",m->db->strings+source->name_off,d->dbc_id,m->db->strings+d->name_off,m->db->strings+def->name_off,i,k);
            printf("\"original_can_id\":%" PRIu32 ",\"extended\":%s,\"pgn\":",d->dbc_id & UINT32_C(0x1fffffff),msg->extended?"true":"false");
            optional(msg->pgn,msg->extended);printf(",\"source_address\":");optional(d->dbc_id &255u,msg->extended);putchar(',');
            printf("\"start_bit\":%u,\"width\":%u,\"flags\":%u,\"mux\":%u,\"unit\":",s->start_bit,s->length,s->flags,s->mux_value);string(j1939_signal_unit(m,def->signal_index));
            printf(",\"numbers\":[");for(x=0;x<4;++x){if(x)putchar(',');string(m->db->strings+literals[x]);}
            printf("],\"bits\":[");for(x=0;x<4;++x){union{double d;uint64_t u;} bits;bits.d=values[x];printf("%s\"%016" PRIx64 "\"",x?",":"",bits.u);}
            printf("],\"enums\":[");for(x=0;x<s->enum_count;++x){const j1939_enum_t *e=&m->db->enums[s->enum_begin+x];printf("%s[\"%" PRIu64 "\",",x?",":"",e->value);string(m->db->strings+e->name_off);putchar(']');}
            printf("],\"spn\":");optional(def->spn,def->flags & J1939_META_HAS_SPN);
            printf(",\"line\":%" PRIu32 ",\"message_dlc\":%u,\"message_line\":%" PRIu32 ",\"message_spn\":",def->line,msg->dlc,d->line);optional(d->spn,d->flags & J1939_META_HAS_SPN);
            printf(",\"default\":%s,\"source_bytes\":\"%" PRIu64 "\",\"source_fnv\":\"%016" PRIx64 "\",\"source_version\":",(d->flags & J1939_META_HAS_DEFAULT)?((d->flags & J1939_META_DEFAULT)?"true":"false"):"null",source->byte_length,source->fnv1a64);
            if(source->version_off==J1939_NO_NAME)printf("null");else string(m->db->strings+source->version_off);
            printf(",\"switch_name\":");
            if(s->flags & J1939_SIG_MULTIPLEXED) {
                for(x=0;x<d->signal_count;++x){const j1939_signal_definition_t *sw=&m->signal_definitions[d->signal_begin+x];
                    if(m->db->signals[sw->signal_index].flags & J1939_SIG_MULTIPLEXOR){string(m->db->strings+sw->name_off);break;}}
            } else printf("null");
            printf("}}");
        }
    }
    puts("]}");return 0;
}
