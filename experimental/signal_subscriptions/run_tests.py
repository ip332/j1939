#!/usr/bin/env python3
"""Host-only compiler + pure-C prototype gates. No capture, Quarry, or network."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def run(command, capture=False):
    print(shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=ROOT, check=True,
                          stdout=subprocess.PIPE if capture else None, text=True,
                          env=dict(os.environ, ASAN_OPTIONS='abort_on_error=1',
                                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build-dir', required=True)
    args = ap.parse_args()
    build = Path(args.build_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get('CC', 'cc'))
    cxx = shlex.split(os.environ.get('CXX', 'c++'))
    version = subprocess.check_output(cxx+['--version'], text=True).lower()
    sources = sorted(p for p in (ROOT/'j1939_parser/dbc_parser').glob('*.cpp')
                     if not p.stem.endswith(('_test', '_tests')))
    host_flags = ['-Wno-deprecated-declarations']
    if 'clang' in version:
        host_flags.append('-Wno-vla-cxx-extension')
    host_flags += shlex.split(os.environ.get('HOST_CXXFLAGS', ''))
    compiler = build/'dbc_compiler'
    run(cxx+['-std=c++14', '-Wall', '-Wextra', '-Werror']+host_flags+
        ['-Ioptions', '-Ij1939_parser', '-Ij1939_parser/dbc_parser',
         'dbc_compiler/dbc_compiler.cpp']+sources+['j1939_parser/j1939_frame.cpp', '-o', compiler])
    run([compiler, '-in', ROOT/'tests/dbc/subscriptions_synthetic.dbc', '-name', 'subscriptions',
         '-c_path', build/'subscriptions.c', '-c-metadata'])
    model_python=os.environ.get('MODEL_PYTHON',sys.executable)
    run(['protoc','-I',ROOT/'schema','--python_out='+str(build),ROOT/'schema/dbc_model_v1.proto'])
    run([compiler,'-in',ROOT/'tests/dbc/subscriptions_synthetic.dbc','-model_path',build/'model.textproto'])
    model_env=dict(os.environ, PYTHONPATH=str(build)+os.pathsep+os.environ.get('PYTHONPATH',''))
    key_command=[model_python,str(HERE/'generate_keys.py'),'--model',str(build/'model.textproto'),
        '--database',str(build/'subscriptions.c'),'--metadata-symbol','subscriptions_metadata',
        '--out-prefix',str(build/'signal_keys')]
    print(shlex.join(key_command),flush=True);subprocess.run(key_command,cwd=ROOT,env=model_env,check=True)
    generation_test=[model_python,str(HERE/'key_generation_tests.py'),str(compiler),str(build)]
    print(shlex.join(generation_test),flush=True);subprocess.run(generation_test,cwd=ROOT,env=model_env,check=True)
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
             '-fsanitize=address,undefined', '-I'+str(ROOT/'j1939_parser'),
             '-I'+str(HERE), '-I'+str(build)]
    for source in ['tests', 'key_tests', 'queue_tests', 'dashboard']:
        run(cc+flags+(['-O2'] if source=='key_tests' else [])+[HERE/(source+'.c'), HERE/'j1939_subscriptions.c',
                      ROOT/'j1939_parser/j1939_db.c', build/'subscriptions.c', HERE/'j1939_signal_keys.c',HERE/'j1939_key_queue.c',HERE/'j1939_latest.c',build/'signal_keys.c', '-o', build/source])
        result=run([build/source], capture=source=='dashboard')
        if source=='dashboard':
            print(result.stdout, end='')
            lines=[json.loads(line) for line in result.stdout.splitlines()]
            binding=lines[0]
            values=[v for v in lines[1:] if v["type"]=="observation"]
            latest=[v for v in lines[1:] if v["type"]=="latest"]
            assert len(latest)==4 and [v["status"] for v in latest]==[0,0,3,3]
            assert [v["engineering"] for v in latest[:2]]==[1000,50]
            assert all("engineering" not in v and "raw" not in v for v in latest[2:])
            assert binding['type']=='metadata' and binding['generation']=='1'
            assert all(value['type']=='observation' and 'catalog_sha256' not in value for value in values)
            assert [v['engineering'] for v in values]==[1000,50,2000,95]
            assert [v['raw'] for v in values]==['8000','125','8000','50']
            assert [v['source'] for v in values]==[11,11,62,11]
            assert values[0]['timestamp_ns']=='1000000000' and values[2]['timestamp_ns'] is None
            manifest=json.loads((build/'signal_keys.json').read_text())
            for value in values:
                # Check the negotiated binding before interpreting the packed bits.
                assert binding['catalog_sha256']==manifest['catalog_sha256']
                assert binding['layout']==[8,8,16] and binding['layout_version']==1 and binding['source_mode']==0
                assert binding['source_namespace']=='0' and binding['mapping_hash']=='cbf29ce484222325'
                domain=(value['packed_key']>>24)&255;source=(value['packed_key']>>16)&255;signal_id=value['packed_key']&65535
                assert (domain,source,signal_id)==(1,value['source_identity'],value['signal_id'])
                assert manifest['signals'][signal_id]['signal']['name']==value['signal']
                assert manifest['signals'][signal_id]['signal']['unit']==value['unit']
                assert binding['layout']==[8,8,16] and binding['layout_version']==1 and binding['source_mode']==0
            print('Dashboard JSON expected values and catalog association PASS', flush=True)
    transitions=[]
    for label,scale,unit in [('old','1','old unit'),('new','2','new unit')]:
        directory=build/label;directory.mkdir(exist_ok=True)
        dbc=directory/'transition.dbc'
        dbc.write_text('BO_ 2566852619 Example: 8 ECU\n SG_ Value : 0|8@1+ ('+scale+',0) [0|510] "'+unit+'" ECU\n')
        run([compiler,'-in',dbc,'-name',label,'-c_path',directory/'database.c','-c-metadata'])
        run([compiler,'-in',dbc,'-model_path',directory/'model.textproto'])
        cmd=[model_python,str(HERE/'generate_keys.py'),'--model',str(directory/'model.textproto'),
             '--database',str(directory/'database.c'),'--metadata-symbol',label+'_metadata',
             '--catalog-name',label+'_keys','--out-prefix',str(build/(label+'_keys'))]
        print(shlex.join(cmd),flush=True);subprocess.run(cmd,cwd=ROOT,env=model_env,check=True)
        transitions += [directory/'database.c',build/(label+'_keys.c')]
    run(cc+flags+[HERE/'double_queue_tests.c',HERE/'j1939_subscriptions.c',HERE/'j1939_signal_keys.c',
        HERE/'j1939_key_queue.c',ROOT/'j1939_parser/j1939_db.c']+transitions+['-o',build/'double_queue_tests'])
    run([build/'double_queue_tests'])
    run(cc+flags+[HERE/'latest_tests.c',HERE/'j1939_subscriptions.c',HERE/'j1939_signal_keys.c',
        HERE/'j1939_key_queue.c',HERE/'j1939_latest.c',ROOT/'j1939_parser/j1939_db.c',build/'subscriptions.c',build/'signal_keys.c']+transitions+['-o',build/'latest_tests'])
    run([build/'latest_tests'])
    print('Prototype and dashboard PASS', flush=True)

if __name__ == '__main__':
    main()
