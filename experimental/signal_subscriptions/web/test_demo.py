"""Independent expectations and localhost protocol checks; no private inputs."""
import copy
import json
import math
from pathlib import Path
import subprocess
import threading
import urllib.request

HERE=Path(__file__).resolve().parent

def check(events,catalogs):
    import run
    by_hash={m['catalog_sha256']:m for m in catalogs}
    activations=[e for e in events if e['type']=='activate']
    assert len(activations)==2 and activations[0]['catalog']!=activations[1]['catalog']
    observations=[e for e in events if e['type']=='observation']
    old=[e for e in observations if e['generation']=='1']
    new=[e for e in observations if e['generation']=='2']
    assert len(old)==13 and len(new)==25
    # Definition names select independent numeric expectations, not another decode path.
    expected={('EngineA','Speed'):(8000,1000,'rpm',11),('EngineA','Temperature'):(254,-41,'deg C',11),
      ('EngineA','Mode'):(1,1,'',11),('EngineA','Load'):(125,50,'%',11),('EngineA','Status'):(255,255,'',11),
      ('EngineB','Speed'):(8000,2000,'rpm',62),('EngineB','Temperature'):(254,-41,'deg C',62),
      ('Pressure','Pressure'):(50,95,'kPa',11),('WideUnsigned','Wide'):(2**64-1,float(2**64-1),'count',11)}
    seen=set()
    for e in old[:9]:
        meta=by_hash[e['catalog']]['signals'][e['signal_id']];name=(meta['message'],meta['signal']['name'])
        raw,engineering,unit,source=expected[name];seen.add(name)
        assert e['raw']==str(raw) and math.isclose(e['engineering'],engineering,rel_tol=1e-15) and e['unit']==unit and e['source']==source
        assert e['clock_id']==7 and e['timestamp_ns'] in ['1000000000','1100000000','1200000000','1300000000']
        assert e['signed_raw']==('-2' if name[1]=='Temperature' else None)
    assert seen==set(expected)
    # The fifth fixture frame disables mux1: only Speed/Temperature/Mode/Status.
    assert len(old[9:])==4
    assert {by_hash[e['catalog']]['signals'][e['signal_id']]['signal']['name'] for e in old[9:]}=={'Speed','Temperature','Mode','Status'}
    assert all(e['timestamp_ns']=='1400000000' for e in old[9:])
    first_old=old[0];first_new=new[0]
    assert first_old['key']==first_new['key'] and first_old['signal_id']==first_new['signal_id']
    assert (first_old['engineering'],first_old['unit'])==(1000,'rpm')
    assert (first_new['engineering'],first_new['unit'])==(2000,'demo rpm')
    # FIFO across handoff: all old observations precede all new ones.
    assert [e['generation'] for e in observations]==['1']*13+['2']*25
    statuses={e['name']:e['status'] for e in events if e['type']=='status'}
    assert statuses=={'old-latest/wrong-catalog':-6,'third-switch/bank-unavailable':-4,
      'normal-transition-drops':0,'latest-fresh':0,'latest-stale':2,'overload-drop-newest':4}
    assert {e['pgn'] for e in old}=={0xff01,0xff02,0xff03} # EngineA/B share PGN but different rows/sources
    assert len({(e['pgn'],e['source']) for e in old})==4
    run.validate(events,catalogs)
    for field,bad in [('catalog','0'*64),('generation','99'),('key',0),('unit','wrong'),('raw',18446744073709551615)]:
        altered=copy.deepcopy(events);target=next(e for e in altered if e['type']=='observation');target[field]=bad
        try:run.validate(altered,catalogs)
        except ValueError:pass
        else:raise AssertionError('accepted '+field+' mismatch')
    http=run.server(events,catalogs,0,0)
    thread=threading.Thread(target=http.serve_forever,daemon=True);thread.start()
    try:
        base='http://127.0.0.1:'+str(http.server_port)
        with urllib.request.urlopen(base+'/catalogs') as response:assert json.load(response)==catalogs
        with urllib.request.urlopen(base+'/events') as response:
            assert response.headers['Content-Type']=='text/event-stream'
            decoded=[json.loads(line[6:]) for line in response.read().decode().splitlines() if line.startswith('data: ')]
            assert decoded==events
        with urllib.request.urlopen(base+'/') as response:assert b'J1939 signal lab' in response.read()
    finally:http.shutdown();http.server_close();thread.join()
    for script in ['test_model.mjs','test_controller.mjs']:
        node=subprocess.run(['node',str(HERE/script)],input=json.dumps({'events':events,'catalogs':catalogs}),text=True,capture_output=True,check=True)
        print(node.stdout,end='')
    print('PASS independent9-signal values/units,3 PGNs/2 SAs/4 routes, mux,timestamps, catalog handoff, stale/BUSY/drop4, raw64/binding rejection and HTTP/SSE replay')
