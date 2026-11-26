#!/usr/bin/env python3
"""Synthetic 100 retained values/30 PGNs; host exercise, object/ABI cross-measurement."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--build-dir',required=True)
    args=ap.parse_args();b=Path(args.build_dir).resolve();b.mkdir(parents=True,exist_ok=True)
    commands=[]
    def run(cmd,env=None):
        cmd=list(map(str,cmd));commands.append(cmd);print(shlex.join(cmd),flush=True)
        return subprocess.check_output(cmd,cwd=ROOT,text=True,env=env)
    print(run([sys.executable,HERE/'measure_keys.py','--compiler',args.compiler,'--build-dir',b]),end='')
    manifest=json.loads((b/'signal_keys.json').read_text())
    lookup={(s['message'],s['signal']['name']):s['constant'] for s in manifest['signals']}
    matrix='static const uint32_t ids[30][4]={\n'
    for m in range(30):
        matrix+=' {'+', '.join(lookup[(f'Message{m}',f'Signal{s}')] if s<(4 if m<10 else 3) else '0' for s in range(4))+'},\n'
    matrix+='};\n'
    source=r'''
#include "signal_keys.h"
#include "j1939_latest.h"
#include <assert.h>
#include <stdio.h>
MATRIX
static const uint8_t stamp[32]=SUB_GENERATED_CATALOG_SHA256;
typedef struct { j1939_key_context_t *c;j1939_latest_t *s;unsigned count; } app_t;
static void receive(void *user,const j1939_key_update_t *u) {
    app_t *a=(app_t *)user;assert(j1939_latest_put(a->s,a->c,u)==0);++a->count;
}
int main(void) {
    j1939_key_context_t c;j1939_sub_slot_t base[100];j1939_key_slot_t keys[100];
    j1939_latest_t store;j1939_latest_slot_t retained[100];app_t app={&c,&store,0};
    j1939_key_layout_t l={8,8,16};j1939_key_filter_t f={1,11,0,0};
    j1939_sub_timestamp_t ts={100,7};j1939_latest_freshness_t policy={{105,7},5};
    uint8_t bytes[8]={1,0,2,0,3,0,4,0};unsigned m,s;
    assert(j1939_keys_init(&c,&generated_keys_catalog,stamp,l,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,keys,100,receive,&app)==0);
    assert(j1939_latest_init(&store,&c,retained,100)==0);
    for(m=0;m<30;++m)for(s=0;s<(m<10?4u:3u);++s){uint32_t token;
        assert(j1939_keys_subscribe(&c,&c.identity,ids[m][s],f,0,&token)==0);}
    for(m=0;m<30;++m){j1939_sub_frame_t frame={0x18ff100b+(m<<8),bytes,&ts,8,1};
        assert(j1939_keys_process(&c,1,&frame,NULL)==0);}
    assert(app.count==100);
    for(m=0;m<30;++m)for(s=0;s<(m<10?4u:3u);++s){j1939_latest_value_t value;
        assert(j1939_latest_get(&store,&c,&c.identity,ids[m][s],1,11,&value)==0);
        assert(value.raw==s+1 && value.engineering==0.125*(s+1)-10);
        assert(j1939_latest_get_with_policy(&store,&c,&c.identity,ids[m][s],1,11,&policy,&value)==0);
        assert(value.raw==s+1 && value.engineering==0.125*(s+1)-10);}
    printf("Latest100/30: 100 exact raw/engineering checks PASS; slot=%zu store=%zu retained100=%zu total_with_subscriptions=%zu policy=%zu\n",
        sizeof(j1939_latest_slot_t),sizeof(store),sizeof(store)+sizeof(retained),
        sizeof(store)+sizeof(retained)+sizeof(c)+sizeof(base)+sizeof(keys),sizeof(policy));
    return 0;
}
'''.replace('MATRIX',matrix)
    (b/'large-latest.c').write_text(source)
    includes=['-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),'-I'+str(b)]
    cc=shlex.split(os.environ.get('CC','cc'))
    run(cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined']+includes+
        [b/'large-latest.c',b/'full.c',b/'signal_keys.c',HERE/'j1939_subscriptions.c',HERE/'j1939_signal_keys.c',
         HERE/'j1939_latest.c',ROOT/'j1939_parser/j1939_db.c','-o',b/'large-latest'])
    print(run([b/'large-latest'],dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')),end='')
    (b/'latest-abi.c').write_text('#include "j1939_latest.h"\nchar latest_slot[sizeof(j1939_latest_slot_t)];\nchar latest_store[sizeof(j1939_latest_t)];\nchar latest_value[sizeof(j1939_latest_value_t)];\nchar latest_policy[sizeof(j1939_latest_freshness_t)];\n')
    results={}
    for target,flags in [('host',[]),('cortex_m4',['--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb','-ffreestanding'])]:
        for name,src in [('runtime',HERE/'j1939_latest.c'),('abi',b/'latest-abi.c')]:
            obj=b/(target+'-latest-'+name+'.o')
            run(['/usr/bin/clang']+flags+['-std=c11','-Os','-ffunction-sections','-fdata-sections']+includes+['-c',src,'-o',obj])
            result=run(['/usr/local/opt/llvm/bin/llvm-size','-A',obj]);(b/(target+'-latest-'+name+'-size.txt')).write_text(result)
            symbols=run(['/usr/local/opt/llvm/bin/llvm-nm','--print-size','--size-sort',obj]);(b/(target+'-latest-'+name+'-symbols.txt')).write_text(symbols)
            if name=='runtime':
                sizes={'text':0,'const':0,'writable':0}
                for line in result.splitlines():
                    a=line.split()
                    if len(a)<2 or not a[1].isdigit():continue
                    sec=a[0];n=int(a[1])
                    if sec=='__text' or sec.startswith('.text'):sizes['text']+=n
                    elif sec.startswith(('.rodata','__const','__cstring')):sizes['const']+=n
                    elif sec in ('__data','__bss','__common') or sec.startswith(('.data','.bss')):sizes['writable']+=n
                results[target]=sizes
    (b/'latest-results.json').write_text(json.dumps(results,indent=2)+'\n')
    (b/'latest-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
