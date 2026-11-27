#!/usr/bin/env python3
"""Host-only build/replay/loopback SSE bridge. No third-party HTTP packages."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
sys.dont_write_bytecode=True
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time

HERE=Path(__file__).resolve().parent
CORE=HERE.parent
ROOT=CORE.parents[1]

def build(directory, compiler=None):
    directory=Path(directory).resolve();directory.mkdir(parents=True,exist_ok=True)
    if compiler is None:
        subprocess.run([sys.executable,CORE/'run_tests.py','--build-dir',directory/'prerequisites'],cwd=ROOT,check=True)
        compiler=directory/'prerequisites/dbc_compiler'
    compiler=Path(compiler).resolve()
    commands=[]
    def run(cmd,env=None):
        cmd=list(map(str,cmd));commands.append(cmd);print(shlex.join(cmd),flush=True)
        subprocess.run(cmd,cwd=ROOT,check=True,env=env)
    run(['protoc','-I',ROOT/'schema','--python_out='+str(directory),ROOT/'schema/dbc_model_v1.proto'])
    env=dict(os.environ,PYTHONPATH=str(directory)+os.pathsep+os.environ.get('PYTHONPATH',''))
    source=(ROOT/'tests/dbc/subscriptions_synthetic.dbc').read_text()
    if source.count('(0.125,0) [0|16383.75] \"rpm\"')!=2:raise ValueError('expected two synthetic generic speed definitions')
    generated=[]
    for label,text in [('live_a',source),('live_b',source.replace('(0.125,0) [0|16383.75] "rpm"','(0.25,0) [0|32767.5] "demo rpm"'))]:
        folder=directory/label;folder.mkdir(exist_ok=True)
        dbc=folder/'subscriptions_synthetic.dbc';dbc.write_text(text)
        run([compiler,'-in',dbc,'-name',label,'-c_path',folder/'database.c','-c-metadata'])
        run([compiler,'-in',dbc,'-model_path',folder/'model.textproto'])
        run([os.environ.get('MODEL_PYTHON',sys.executable),CORE/'generate_keys.py','--model',folder/'model.textproto',
             '--database',folder/'database.c','--metadata-symbol',label+'_metadata','--catalog-name',label+'_keys',
             '--out-prefix',directory/(label+'_keys')],env)
        generated += [folder/'database.c',directory/(label+'_keys.c')]
    fixture=json.loads((HERE/'frames.json').read_text());data=[];times=[];frames=[]
    for i,frame in enumerate(fixture['frames']):
        payload=bytes.fromhex(frame['data']);stamp=int(frame['timestamp_ns']);can_id=int(frame['can_id'],16)
        if len(payload)!=8 or not 0<=stamp<2**64 or not 0<=can_id<2**29:raise ValueError('invalid synthetic frame')
        data.append('{'+','.join(map(str,payload))+'}')
        times.append('{UINT64_C('+str(stamp)+'),'+str(fixture['clock_id'])+'}')
        frames.append('{UINT32_C('+str(can_id)+'),payloads['+str(i)+'],&times['+str(i)+'],8,1}')
    if fixture['domain']!=1 or fixture['clock_id']!=7:raise ValueError('demo expects domain1/clock7')
    (directory/'frames.h').write_text('#define FRAME_COUNT '+str(len(frames))+'u\nstatic const uint8_t payloads[][8]={'+','.join(data)+'};\nstatic const j1939_sub_timestamp_t times[]={'+','.join(times)+'};\nstatic const j1939_sub_frame_t demo_frames[]={'+','.join(frames)+'};\n')
    cc=shlex.split(os.environ.get('CC','cc'))
    run(cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-UNDEBUG','-fsanitize=address,undefined',
        '-I'+str(ROOT/'j1939_parser'),'-I'+str(CORE),'-I'+str(directory),HERE/'producer.c',
        CORE/'j1939_subscriptions.c',CORE/'j1939_signal_keys.c',CORE/'j1939_key_queue.c',CORE/'j1939_latest.c',
        ROOT/'j1939_parser/j1939_db.c',*generated,'-o',directory/'producer'])
    result=subprocess.run([str(directory/'producer')],check=True,text=True,capture_output=True,
        env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    events=[json.loads(line) for line in result.stdout.splitlines()]
    catalogs=[json.loads((directory/(name+'_keys.json')).read_text()) for name in ['live_a','live_b']]
    validate(events,catalogs)
    (directory/'events.json').write_text(json.dumps(events,indent=2)+'\n')
    (directory/'catalogs.json').write_text(json.dumps(catalogs,indent=2)+'\n')
    (directory/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    return events,catalogs

def validate(events,catalogs):
    """Check session association before serving browser data; consistency, not auth."""
    by_hash={m['catalog_sha256']:m for m in catalogs};bindings={}
    for e in events:
        if e['type']=='activate':
            if e['catalog'] not in by_hash or e['layout']!=[8,8,16] or e['version']!=1 or e['source_mode']!=0 or e['namespace']!='0' or e['mapping']!='cbf29ce484222325':raise ValueError('unsupported/wrong binding')
            bindings[(e['generation'],e['catalog'])]=e
        elif e['type']=='observation':
            if (e['generation'],e['catalog']) not in bindings:raise ValueError('unknown observation binding')
            if not isinstance(e['raw'],str) or not 0<=int(e['raw'])<2**64:raise ValueError('raw must be an exact integer string')
            key=e['key'];sid=e['signal_id']
            if not isinstance(key,int) or not 0<=key<2**32 or (key>>24,(key>>16)&255,key&65535)!=(e['domain'],e['source'],sid):raise ValueError('packed key mismatch')
            m=by_hash[e['catalog']]
            if not 0<=sid<len(m['signals']) or m['signals'][sid]['signal']['unit']!=e['unit']:raise ValueError('wrong signal metadata')
            if not isinstance(e['timestamp_ns'],str) or not 0<=int(e['timestamp_ns'])<2**64 or e['clock_id']!=7:raise ValueError('invalid timestamp context')

def server(events,catalogs,port=8765,delay=0.12):
    validate(events,catalogs)
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path=='/events':
                self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Cache-Control','no-store');self.end_headers()
                try:
                    for event in events:
                        self.wfile.write(('data: '+json.dumps(event,separators=(',',':'))+'\n\n').encode());self.wfile.flush()
                        if delay:time.sleep(delay)
                except (BrokenPipeError,ConnectionResetError):pass
                return
            paths={'/':('index.html','text/html'),'/dashboard.mjs':('dashboard.mjs','text/javascript'),'/model.mjs':('model.mjs','text/javascript')}
            if self.path=='/catalogs':data=json.dumps(catalogs).encode();kind='application/json'
            elif self.path in paths:
                name,kind=paths[self.path];data=(HERE/name).read_bytes()
            else:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Type',kind+'; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    return ThreadingHTTPServer(('127.0.0.1',port),Handler)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--build-dir',required=True);ap.add_argument('--compiler');ap.add_argument('--port',type=int,default=8765);ap.add_argument('--test',action='store_true');ap.add_argument('--delay',type=float,default=0.12)
    args=ap.parse_args()
    if not 0<=args.delay<=2:ap.error('delay must be0..2 seconds')
    events,catalogs=build(args.build_dir,args.compiler)
    if args.test:
        spec=importlib.util.spec_from_file_location('checks',HERE/'test_demo.py');checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks);checks.check(events,catalogs);return
    http=server(events,catalogs,args.port,args.delay);print(f'Dashboard http://127.0.0.1:{http.server_port} — Ctrl-C to stop',flush=True)
    try:http.serve_forever()
    except KeyboardInterrupt:pass
    finally:http.server_close()
if __name__=='__main__':main()
