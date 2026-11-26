#!/usr/bin/env python3
"""Synthetic 100-subscription/30-PGN exercise + object section/ABI measurements."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--compiler', required=True)
    ap.add_argument('--build-dir', required=True)
    ap.add_argument('--llvm-size', default=shutil.which('llvm-size') or '/usr/local/opt/llvm/bin/llvm-size')
    ap.add_argument('--llvm-nm', default=shutil.which('llvm-nm') or '/usr/local/opt/llvm/bin/llvm-nm')
    args = ap.parse_args()
    b = Path(args.build_dir).resolve(); b.mkdir(parents=True, exist_ok=True)
    commands = []
    def run(cmd, sanitize=False):
        cmd = list(map(str, cmd)); commands.append(cmd)
        print(shlex.join(cmd), flush=True)
        env = dict(os.environ)
        if sanitize: env.update(ASAN_OPTIONS='abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        return subprocess.check_output(cmd, cwd=ROOT, env=env, text=True)
    # Redistributable generated test input: 30 unique PGNs, 100 integer signals.
    text = 'VERSION "synthetic 100 subscription catalog"\n\n'
    for m in range(30):
        text += f'BO_ {0x98ff100b+(m<<8)} Message{m}: 8 ECU\n'
        for s in range(4 if m < 10 else 3):
            text += f' SG_ Signal{s} : {16*s}|16@1'+('-' if s == 1 else '+')+' (0.125,-10) [-4106|8181] "'+('deg C' if s == 1 else '')+'" ECU\n'
        text += '\n'
    (b/'catalog.dbc').write_text(text)
    for profile in ['compact', 'full']:
        cmd = [args.compiler, '-in', b/'catalog.dbc', '-c_path', b/(profile+'.c')]
        if profile == 'full': cmd += ['-name', 'catalog', '-c-metadata']
        run(cmd)
    (b/'large.c').write_text(r'''
#include "j1939_subscriptions.h"
#include "full.h"
#include <assert.h>
#include <stdio.h>
static unsigned count;
static void receive(void *user, const j1939_sub_update_t *u) {
    unsigned ordinal=u->tag%4;
    (void)user;
    assert(u->raw==ordinal+1 && u->engineering==0.125*(ordinal+1)-10);
    assert(u->source_address==11 && u->pgn==0xff10+u->tag/4);
    ++count;
}
int main(void) {
    j1939_sub_context_t ctx; j1939_sub_slot_t slots[100];
    uint8_t bytes[8]={1,0,2,0,3,0,4,0}; unsigned m,s;
    assert(j1939_sub_init(&ctx,&catalog_metadata,slots,100,receive,NULL)==0);
    for(m=0;m<30;++m) for(s=0;s<(m<10?4u:3u);++s) {
        char msg[32],sig[32]; uint32_t id; j1939_sub_ref_t ref;
        (void)snprintf(msg,sizeof msg,"Message%u",m);(void)snprintf(sig,sizeof sig,"Signal%u",s);
        assert(j1939_sub_select(&ctx,"catalog.dbc",msg,sig,&ref)==0);
        assert(j1939_subscribe(&ctx,ref,J1939_SUB_DECLARED_SOURCE,4*m+s,&id)==0);
    }
    for(m=0;m<30;++m) {
        j1939_sub_frame_t f={0}; uint16_t n;
        f.can_id=0x18ff100b+(m<<8);f.extended=1;f.data=bytes;f.size=8;
        assert(j1939_sub_process(&ctx,&f,&n)==0 && n==(m<10?4:3));
    }
    assert(count==100);
    puts("100 subscriptions, 30 PGNs, 30 frames, 100 expected updates PASS");return 0;
}
''')
    includes = ['-I'+str(ROOT/'j1939_parser'), '-I'+str(HERE), '-I'+str(b)]
    cc = shlex.split(os.environ.get('CC', 'cc'))
    print(run(cc+['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-fsanitize=address,undefined']+
              includes+[b/'large.c', b/'full.c', HERE/'j1939_subscriptions.c',
                        ROOT/'j1939_parser/j1939_db.c', '-o', b/'large']), end='')
    print(run([b/'large'], sanitize=True), end='')
    (b/'abi.c').write_text('''#include "j1939_subscriptions.h"
char abi_context[sizeof(j1939_sub_context_t)];
char abi_slot[sizeof(j1939_sub_slot_t)];
char abi_update[sizeof(j1939_sub_update_t)];
char abi_ref[sizeof(j1939_sub_ref_t)];
char ram_100_slots[100*sizeof(j1939_sub_slot_t)];
char ram_queue8[8*sizeof(j1939_sub_update_t)];
''')
    result = {}
    for target, flags in [('host', []), ('cortex_m4', ['--target=arm-none-eabi', '-mcpu=cortex-m4', '-mthumb', '-ffreestanding'])]:
        result[target] = {}
        for name, source in [('compact',b/'compact.c'), ('full',b/'full.c'), ('prototype',HERE/'j1939_subscriptions.c'), ('abi',b/'abi.c')]:
            obj=b/(target+'-'+name+'.o')
            run(['clang']+flags+['-std=c11', '-Os', '-ffunction-sections', '-fdata-sections']+includes+['-c', source, '-o', obj])
            sizes=run([args.llvm_size, '-A', obj]); (b/(target+'-'+name+'-size.txt')).write_text(sizes)
            if target == 'cortex_m4':
                sections = {'text':0, 'rodata':0, 'unwind':0, 'writable':0}
                for line in sizes.splitlines():
                    parts=line.split()
                    if len(parts)<2 or not parts[1].isdigit(): continue
                    kind= ('text' if parts[0].startswith('.text') else 'rodata' if parts[0].startswith('.rodata')
                           else 'unwind' if parts[0].startswith('.ARM.exidx') else 'writable' if parts[0].startswith(('.data','.bss')) else None)
                    if kind: sections[kind] += int(parts[1])
            else:
                sections = {'text':0, 'const':0, 'unwind':0, 'writable':0}
                for line in sizes.splitlines():
                    parts=line.split()
                    if len(parts)<2 or not parts[1].isdigit(): continue
                    kind= ('text' if parts[0]=='__text' else 'const' if parts[0].startswith(('__const','__literal','__cstring'))
                           else 'unwind' if parts[0]=='__compact_unwind' else 'writable' if parts[0] in ('__data','__bss','__common') else None)
                    if kind: sections[kind] += int(parts[1])
            result[target][name] = sections
            if name == 'abi' and target == 'cortex_m4':
                symbols=run([args.llvm_nm, '--print-size', '--size-sort', obj]);(b/(target+'-abi-symbols.txt')).write_text(symbols)
                result[target]['abi_sizes']={parts[-1].lstrip('_'):int(parts[1],16) for line in symbols.splitlines()
                    if len(parts:=line.split())>=4 and parts[-1].lstrip('_').startswith(('abi_','ram_'))}
            if name == 'prototype':
                undef=run([args.llvm_nm, '-u', obj]);(b/(target+'-undefined.txt')).write_text(undef)
    (b/'host-abi.c').write_text('#include "j1939_subscriptions.h"\n#include <stdio.h>\nint main(void) {\n printf("{\\"abi_context\\":%zu,\\"abi_slot\\":%zu,\\"abi_update\\":%zu,\\"abi_ref\\":%zu,\\"ram_100_slots\\":%zu,\\"ram_queue8\\":%zu}\\n", sizeof(j1939_sub_context_t), sizeof(j1939_sub_slot_t), sizeof(j1939_sub_update_t), sizeof(j1939_sub_ref_t), 100*sizeof(j1939_sub_slot_t), 8*sizeof(j1939_sub_update_t));\n return 0;\n}\n')
    run(cc+['-std=c11']+includes+[b/'host-abi.c', '-o', b/'host-abi'])
    result['host']['abi_sizes'] = json.loads(run([b/'host-abi']))
    (b/'results.json').write_text(json.dumps(result,indent=2));(b/'commands.json').write_text(json.dumps(commands,indent=2))
    print(json.dumps(result,indent=2))

if __name__ == '__main__': main()
