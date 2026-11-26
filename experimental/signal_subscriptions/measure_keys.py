#!/usr/bin/env python3
"""Compare legacy and packed identities on the SAME synthetic100/30 catalog."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',required=True);ap.add_argument('--build-dir',required=True)
    ap.add_argument('--llvm-size',default='/usr/local/opt/llvm/bin/llvm-size');ap.add_argument('--llvm-nm',default='/usr/local/opt/llvm/bin/llvm-nm');args=ap.parse_args()
    b=Path(args.build_dir).resolve();b.mkdir(parents=True,exist_ok=True);commands=[]
    def run(cmd,env=None):
        cmd=list(map(str,cmd));commands.append(cmd);print(shlex.join(cmd),flush=True)
        return subprocess.check_output(cmd,cwd=ROOT,text=True,env=env)
    print(run([sys.executable,HERE/'measure.py','--compiler',args.compiler,'--build-dir',b,'--llvm-size',args.llvm_size,'--llvm-nm',args.llvm_nm]),end='')
    run([args.compiler,'-in',b/'catalog.dbc','-model_path',b/'model.textproto'])
    model_env=dict(os.environ,PYTHONPATH=str(b)+os.pathsep+os.environ.get('PYTHONPATH',''))
    run(['protoc','-I',ROOT/'schema','--python_out='+str(b),ROOT/'schema/dbc_model_v1.proto'])
    run([os.environ.get('MODEL_PYTHON',sys.executable),HERE/'generate_keys.py','--model',b/'model.textproto',
         '--database',b/'full.c','--metadata-symbol','catalog_metadata','--out-prefix',b/'signal_keys'],env=model_env)
    manifest=json.loads((b/'signal_keys.json').read_text());lookup={(s['message'],s['signal']['name']):s['constant'] for s in manifest['signals']}
    matrix='static const uint32_t ids[30][4]={\n'
    for m in range(30):matrix+=' {'+', '.join(lookup[(f'Message{m}',f'Signal{s}')] if s<(4 if m<10 else 3) else '0' for s in range(4))+'},\n'
    matrix+='};\n'
    source=r'''
#include "signal_keys.h"
static const uint8_t expected_catalog[32]=SUB_GENERATED_CATALOG_SHA256;
#include <assert.h>
#include <stdio.h>
MATRIX
static unsigned count;
static void receive(void *user,const j1939_key_update_t *u) {
    unsigned m=u->value.tag/4,s=u->value.tag%4;(void)user;
    assert(u->value.raw==s+1 && u->value.engineering==0.125*(s+1)-10);
    assert(u->value.pgn==0xff10+m && u->signal_id==ids[m][s]);
    assert(u->packed_key==((u->domain<<24)|(u->source_identity<<16)|ids[m][s]));
    ++count;
}
int main(void) {
    j1939_key_context_t ctx;j1939_sub_slot_t base[100];j1939_key_slot_t slots[100];
    j1939_key_layout_t l={8,8,16};uint8_t bytes[8]={1,0,2,0,3,0,4,0};unsigned m,s;
    assert(j1939_keys_init(&ctx,&generated_keys_catalog,expected_catalog,l,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,100,receive,NULL)==0);
    for(m=0;m<30;++m)for(s=0;s<(m<10?4u:3u);++s) {
        uint32_t token,key;assert(j1939_key_pack(l,1,11,ids[m][s],&key)==0);
        assert(j1939_keys_subscribe_key(&ctx,&ctx.identity,key,4*m+s,&token)==0);
    }
    for(m=0;m<30;++m) {
        j1939_sub_frame_t f={0};uint16_t n;f.can_id=0x18ff100b+(m<<8);f.extended=1;f.data=bytes;f.size=8;
        assert(j1939_keys_process(&ctx,1,&f,&n)==0 && n==(m<10?4:3));
    }
    assert(count==100);
    assert(j1939_keys_init(&ctx,&generated_keys_catalog,expected_catalog,l,J1939_KEY_SOURCE_ADDRESS,0,NULL,0,base,slots,1,receive,NULL)==0);
    {uint32_t token;uint16_t n;j1939_sub_frame_t f={0};assert(ids[0][0]==0);
     assert(j1939_keys_subscribe_key(&ctx,&ctx.identity,0,0,&token)==0);
     f.can_id=0x18ff1000;f.extended=1;f.data=bytes;f.size=8;
     assert(j1939_keys_process(&ctx,0,&f,&n)==0 && n==1 && count==101);}
    puts("Packed:100 subscriptions/30 PGNs/100 expected keys PASS; actual zero key PASS");return 0;
}
'''.replace('MATRIX',matrix)
    (b/'large-keys.c').write_text(source);includes=['-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),'-I'+str(b)]
    cc=shlex.split(os.environ.get('CC','cc'));env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    run(cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined']+includes+
        [b/'large-keys.c',b/'full.c',b/'signal_keys.c',HERE/'j1939_subscriptions.c',HERE/'j1939_signal_keys.c',ROOT/'j1939_parser/j1939_db.c','-o',b/'large-keys'])
    print(run([b/'large-keys'],env=env),end='')
    (b/'key-abi.c').write_text('#include "j1939_key_queue.h"\nchar packed_context[sizeof(j1939_key_context_t)];\nchar packed_slot[sizeof(j1939_key_slot_t)];\nchar packed_update[sizeof(j1939_key_update_t)];\nchar packed_identity[sizeof(j1939_key_identity_t)];\nchar packed_mapping[sizeof(j1939_key_mapping_t)];\nchar packed_queue[sizeof(j1939_key_queue_t)];\nchar packed_blob[sizeof(j1939_key_queue_metadata_t)];\nchar packed_record[sizeof(j1939_key_record_t)];\nchar packed_pair[sizeof(j1939_key_pair_t)];\n')
    result={'baseline':json.loads((b/'results.json').read_text()),'packed':{}}
    for target,flags in [('host',[]),('cortex_m4',['--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb','-ffreestanding'])]:
        sizes={};result['packed'][target]={}
        for name,path in [('runtime',HERE/'j1939_signal_keys.c'),('queue',HERE/'j1939_key_queue.c'),('catalog',b/'signal_keys.c'),('abi',b/'key-abi.c')]:
            obj=b/(target+'-keys-'+name+'.o')
            run(['clang']+flags+['-std=c11','-Os','-ffunction-sections','-fdata-sections']+includes+['-c',path,'-o',obj])
            text=run([args.llvm_size,'-A',obj]);(b/(target+'-keys-'+name+'-size.txt')).write_text(text)
            values={'text':0,'const':0,'unwind':0,'writable':0}
            for line in text.splitlines():
                parts=line.split()
                if len(parts)<2 or not parts[1].isdigit():continue
                sec=parts[0]
                category=('text' if sec=='__text' or sec.startswith('.text') else 'const' if sec.startswith(('.rodata','__const','__literal','__cstring')) else 'unwind' if sec=='__compact_unwind' or sec.startswith('.ARM.exidx') else 'writable' if sec in ('__data','__bss','__common') or sec.startswith(('.data','.bss')) else None)
                if category:values[category]+=int(parts[1])
            result['packed'][target][name]=values
            if name=='abi' and target=='cortex_m4':
                symbols=run([args.llvm_nm,'--print-size','--size-sort',obj]);(b/'packed-arm-symbols.txt').write_text(symbols)
                sizes={p[-1]:int(p[1],16) for line in symbols.splitlines() if len(p:=line.split())>=4 and p[-1].startswith('packed_')}
            if name in ('runtime','queue'):(b/(target+'-keys-'+name+'-undefined.txt')).write_text(run([args.llvm_nm,'-u',obj]))
        if target=='host':
            c='#include "j1939_key_queue.h"\n#include <stdio.h>\nint main(void){printf("{\\"packed_context\\":%zu,\\"packed_slot\\":%zu,\\"packed_update\\":%zu,\\"packed_identity\\":%zu,\\"packed_mapping\\":%zu,\\"packed_queue\\":%zu,\\"packed_blob\\":%zu,\\"packed_record\\":%zu,\\"packed_pair\\":%zu}\\n",sizeof(j1939_key_context_t),sizeof(j1939_key_slot_t),sizeof(j1939_key_update_t),sizeof(j1939_key_identity_t),sizeof(j1939_key_mapping_t),sizeof(j1939_key_queue_t),sizeof(j1939_key_queue_metadata_t),sizeof(j1939_key_record_t),sizeof(j1939_key_pair_t));return 0;}\n'
            (b/'packed-host-abi.c').write_text(c);run(cc+['-std=c11']+includes+[b/'packed-host-abi.c','-o',b/'packed-host-abi']);sizes=json.loads(run([b/'packed-host-abi']))
        old=result['baseline'][target]['abi_sizes'];sizes['ram_100']=sizes['packed_context']+100*(sizes['packed_slot']+old['abi_slot'])
        sizes['queue8_ram']=sizes['packed_queue']+sizes['packed_blob']+8*sizes['packed_record']
        sizes['double_queue8_ram']=2*sizes['queue8_ram']+sizes['packed_pair']
        sizes['double_100_ram']=2*sizes['ram_100']+sizes['double_queue8_ram']
        result['packed'][target]['sizes']=sizes
    (b/'packed-results.json').write_text(json.dumps(result,indent=2));(b/'packed-commands.json').write_text(json.dumps(commands,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
