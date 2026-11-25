#!/usr/bin/env python3
"""Exhaustive observed-frame validation. Private capture artifacts stay outside repos.
Uses the existing DBC parser/C codec, Quarry-generated C producer, owning C++ consumer.
No runtime DBC parsing, CAN I/O, array batching requirement, or quality inference.
"""
import argparse
from collections import Counter, defaultdict
from decimal import Decimal, localcontext
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
QUARRY = Path(os.environ.get('QUARRY_SOURCE', ROOT.parent / 'quarry')).resolve()
# Optional reviewed-input allowlist is explicitly supplied outside the source tree.
# It is a parser/reader review gate, not authentication.
REVIEWED_VENDOR = set()
REVIEWED_CAPTURE = None


def load_private_profile(path):
    profile = json.loads(path.read_text())
    if set(profile) != {'reviewed_dbc_sha256', 'reviewed_capture_sha256'}:
        raise ValueError('private profile requires reviewed_dbc_sha256 and reviewed_capture_sha256 only')
    hashes = profile['reviewed_dbc_sha256']
    capture_hash = profile['reviewed_capture_sha256']
    if not isinstance(hashes,list) or not hashes or any(not isinstance(h,str) or not re.fullmatch('[0-9a-f]{64}',h) for h in hashes) or not isinstance(capture_hash,str) or not re.fullmatch('[0-9a-f]{64}',capture_hash):
        raise ValueError('private profile requires reviewed lowercase SHA-256 input fingerprints')
    return set(hashes), capture_hash


def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def save(p, value):
    Path(p).write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def run(args, **kw):
    return subprocess.run(list(map(str, args)), check=True, **kw)


def capture(args, **kw):
    return subprocess.check_output(list(map(str, args)), text=True, **kw)


def unpack(cid):
    pf, ps = (cid >> 16) & 255, (cid >> 8) & 255
    return (((cid >> 24) & 1) << 16) | (pf << 8) | (ps if pf >= 240 else 0), cid & 255, ps if pf < 240 else 255


def bits(payload, signal):
    """Independent oracle: Intel integer shift or Motorola sawtooth bit walk."""
    width, start = signal['width'], signal['start_bit']
    if signal['byte_order'] == 0:
        raw = (int.from_bytes(payload, 'little') >> start) & ((1 << width) - 1)
    else:
        raw, pos = 0, start
        for _ in range(width):
            raw = (raw << 1) | ((payload[pos // 8] >> (pos % 8)) & 1)
            pos = pos + 15 if pos % 8 == 0 else pos - 1
    return raw


def active(payload, signal, signals):
    return not signal['multiplexed'] or bits(payload, next(s for s in signals if s['is_mux_switch'])) == signal['mux_value']


def raw_value(payload, signal):
    value = bits(payload, signal)
    if signal['is_signed'] and value & (1 << (signal['width'] - 1)):
        value -= 1 << signal['width']
    return value


def validate_definition(m):
    if not 1 <= m['dlc'] <= 8:
        raise ValueError('DLC outside classic single-frame 1..8: reassembly/FD policy needed')
    if not m['signals']:
        raise ValueError('no signals; refusing incomplete schema')
    names, occupied = set(), []
    switches = [s for s in m['signals'] if s['is_mux_switch']]
    if len(switches) > 1 or any(s['multiplexed'] or s['is_signed'] for s in switches):
        raise ValueError('requires one unsigned non-nested mux switch')
    for s in m['signals']:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', s['name']) or s['name'] in names:
            raise ValueError('invalid/duplicate BRD field name: ' + s['name'])
        names.add(s['name'])
        if not 1 <= s['width'] <= 64:
            raise ValueError('integer width outside 1..64: ' + s['name'])
        if s['multiplexed'] and (not switches or s['mux_value'] >= 1 << switches[0]['width']):
            raise ValueError('missing/unrepresentable mux switch: ' + s['name'])
        for key in ('scale', 'offset', 'minimum', 'maximum'):
            if not Decimal(s[key]).is_finite():
                raise ValueError('non-finite ' + key + ': ' + s['name'])
        if Decimal(s['scale']) == 0:
            raise ValueError('zero scale: ' + s['name'])
        positions, pos = set(), s['start_bit']
        for _ in range(s['width']):
            if not 0 <= pos < 8 * m['dlc']:
                raise ValueError('signal outside DLC: ' + s['name'])
            positions.add(pos)
            pos = pos + 1 if s['byte_order'] == 0 else pos + 15 if pos % 8 == 0 else pos - 1
        for other, used in occupied:
            exclusive = s['multiplexed'] and other['multiplexed'] and s['mux_value'] != other['mux_value']
            if positions & used and not exclusive:
                raise ValueError('overlapping active signals: ' + s['name'])
        occupied.append((s, positions))


def signature(m):
    # Declaration order is a schema decision. Provenance is excluded from equivalence.
    return canonical({'dlc': m['dlc'], 'signals': [{k: v for k, v in s.items() if k != 'line'} for s in m['signals']]})


def select(candidates, frame):
    if not frame['extended']:
        raise ValueError('standard frame; no J1939 PGN mapping')
    if frame.get('fd') or frame['dlc'] > 8:
        raise ValueError('CAN FD/oversized frame unsupported')
    if frame['pgn'] in (0xec00, 0xeb00):
        raise ValueError('transport fragment; reassemble before mapping')
    if not candidates:
        raise ValueError('no DBC definition')
    request_default=[m for m in candidates if m.get('request_default')]
    if request_default:
        # Reviewed ISO Request profile: one 24-bit little-endian PGN, independent of SA/DA.
        for candidate in candidates:
            signals=candidate['signals']
            if candidate['dlc']!=3 or len(signals)!=1 or any(signals[0][k]!=v for k,v in {'start_bit':0,'width':24,'byte_order':0,'is_signed':False}.items()) or Decimal(signals[0]['scale'])!=1 or Decimal(signals[0]['offset'])!=0:
                raise ValueError('Request default conflicts with non-ISO-Request layout; review routing')
        interpretations = {canonical({k:v for k,v in candidate['signals'][0].items()
                                      if k not in ('name','line')}) for candidate in candidates}
        if len(interpretations) != 1:
            raise ValueError('Request default has conflicting interpretation metadata; review routing')
        m=request_default[0];validate_definition(m)
        if frame['dlc']!=3:raise ValueError('short/incorrect DLC for ISO Request; expected 3')
        return m,request_default,'reviewed ISO Request primary default, all sources and destinations'
    groups = defaultdict(list)
    for m in candidates:
        groups[signature(m)].append(m)
    if len(groups) == 1:
        aliases = candidates
        if any(m.get('declared_sources_only') for m in aliases):
            if frame['sa'] not in {m['sa'] for m in aliases}:
                raise ValueError('supplemental proprietary definition requires a declared source address')
            policy='declared supplemental source addresses only'
        else:
            policy = 'equivalent definitions; generic PGN layout'
    else:
        exact = [m for m in candidates if m['sa'] == frame['sa']]
        if len({signature(m) for m in exact}) != 1:
            raise ValueError('conflicting definitions: no unique exact source-address match; configure routing/default')
        aliases = groups[signature(exact[0])]
        policy = 'conflicting PGN definitions; exact DBC source-address match only'
    m = aliases[0]
    validate_definition(m)
    if frame['dlc'] != m['dlc']:
        raise ValueError(f"short/incorrect DLC {frame['dlc']}; expected {m['dlc']}")
    return m, aliases, policy


def compile_tools(build, sanitize, reuse=None):
    includes = ['-I' + str(ROOT / p) for p in ('options', 'j1939_parser', 'j1939_parser/dbc_parser', 'can_stream')]
    sources = sorted(p for p in (ROOT / 'j1939_parser/dbc_parser').glob('*.cpp') if not p.name.endswith(('_test.cpp', '_tests.cpp')))
    flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if sanitize else []
    stamp = hashlib.sha256(canonical(dict(sanitize=sanitize, compiler=capture(['c++','--version']), sources={str(p.relative_to(ROOT)): digest(p) for p in [HERE/'project.cpp', HERE/'headers.cpp', *sources, *sorted((ROOT/'j1939_parser/dbc_parser').glob('*.h')), ROOT/'j1939_parser/j1939_frame.cpp', ROOT/'j1939_parser/j1939_frame.h']})).encode()).hexdigest()
    if reuse:
        import shutil
        info = json.loads((reuse/'tools.json').read_text())
        if info['stamp'] != stamp or any(digest(reuse/name) != h for name,h in info['executables'].items()):
            raise ValueError('projection tool source/build mismatch; rebuild without --tools-from')
        for name in info['executables']:
            if (reuse/name).resolve() != (build/name).resolve():
                shutil.copy2(reuse/name, build/name)
        save(build/'tools.json',info)
        return includes, flags
    run(['c++', '-std=c++14', '-Wno-deprecated-declarations', *flags, *includes, HERE/'project.cpp', *sources, ROOT/'j1939_parser/j1939_frame.cpp', '-o', build/'project'])
    run(['c++', '-std=c++14', '-Wno-deprecated-declarations', *includes, HERE/'headers.cpp', ROOT/'j1939_parser/dbc_parser/dbc_message.cpp', ROOT/'j1939_parser/dbc_parser/dbc_signal.cpp', ROOT/'j1939_parser/j1939_frame.cpp', '-o', build/'headers'])
    save(build/'tools.json',dict(stamp=stamp,executables={name:digest(build/name) for name in ('project','headers')}))
    return includes, flags


def project(build, dbc, destination=None):
    destination = destination or build
    destination.mkdir(parents=True,exist_ok=True)
    # Legacy input is gated to the reviewed vendor specimen or a small explicit dialect.
    text = dbc.read_text()
    if digest(dbc) not in REVIEWED_VENDOR:
        for line_no, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                continue
            if not (line.startswith(('BO_ ', ' SG_ ', 'VAL_ ', 'BA_ "SPN" ', 'BA_ "J1939Default" ', 'VERSION ', 'BU_:'))):
                raise ValueError(f'{dbc}:{line_no}: unreviewed DBC declaration; use strict model validation or review dialect support')
    for line_no, line in enumerate(text.splitlines(), 1):
        if line.startswith(('SIG_VALTYPE_', 'SG_MUL_VAL_', 'VAL_TABLE_', 'SIG_GROUP_')):
            raise ValueError(f'{dbc}:{line_no}: unsupported semantic declaration')
    command=[build/'project', dbc, destination/'database.c', destination/'database.h', *([] if digest(dbc) in REVIEWED_VENDOR else ['--strict'])]
    text_model=capture(command)
    before=[digest(destination/name) for name in ('database.c','database.h')]
    if capture(command)!=text_model or before != [digest(destination/name) for name in ('database.c','database.h')]:
        raise ValueError('nondeterministic DBC projection/database generation')
    messages = json.loads(text_model)
    # Only declaration locations are scanned here. All signal semantics come from DbcParser.
    locations, current = {}, None
    for line_no, line in enumerate(text.splitlines(), 1):
        if line.startswith('BO_ '):
            current = int(line.split()[1])
            if current in locations:
                raise ValueError('duplicate BO_ identity in source')
            locations[current] = {'line': line_no, 'signals': {}}
        elif line.startswith(' SG_ '):
            locations[current]['signals'][line.split()[1]] = line_no
    for m in messages:
        m['line'] = locations[m['id']]['line']
        for s in m['signals']:
            s['line'] = locations[m['id']]['signals'][s['name']]
    return sorted(messages, key=lambda m: m['line'])


def read_capture(build, path, reader_sources, includes, flags):
    original = digest(path)
    if original != REVIEWED_CAPTURE:
        raise ValueError('legacy RosStream is gated to a reviewed capture hash; harden/validate the reader before accepting another capture')
    # Safe-load/validate the complete classic capture before feeding the legacy reader.
    run(['/usr/bin/ruby', HERE/'ros_to_out.rb', path, build/'capture.out', build/'capture.tsv'])
    converted = (build/'capture.out').read_text().splitlines()
    metadata = []
    for n, line in enumerate((build/'capture.tsv').read_text().splitlines()):
        ordinal, secs, ns, bus, status, flag = line.split('\t')
        ordinal, secs, ns, status, flag = map(int, (ordinal, secs, ns, status, flag))
        if ordinal != n or not 0 <= secs <= 0xffffffff or not 0 <= ns < 10**9 or flag not in (0, 4):
            raise ValueError(f'frame {n}: unsupported flags/time; legacy ROS reader not invoked')
        metadata.append(dict(time_ns=secs*10**9+ns, secs=secs, nsecs=ns, bus=bus, status=status, flag=flag))
    if len(metadata) != len(converted):
        raise ValueError('capture metadata/frame count mismatch')
    (build/'compat/linux').mkdir(parents=True, exist_ok=True)
    (build/'compat/linux/can.h').write_text('struct canfd_frame;\n')
    for name in ('ros_stream.cpp', 'ros_stream.h'):
        (build/name).write_bytes((reader_sources/name).read_bytes())
    stream_sources = [ROOT/'can_stream'/n for n in ('can_stream.cpp', 'out_stream.cpp', 'asc_stream.cpp', 'log_stream.cpp', 'trc_stream.cpp', 'txt_stream.cpp', 'actisense_stream.cpp')]
    run(['cc', '-std=c11', *flags, '-I'+str(ROOT/'j1939_parser'), '-c', ROOT/'j1939_parser/j1939_db.c', '-o', build/'decode.o'])
    run(['c++', '-std=c++17', '-O1', '-include', 'cstring', *flags, '-I'+str(build), '-I'+str(build/'compat'), *includes, HERE/'read_ros.cpp', build/'ros_stream.cpp', *stream_sources, ROOT/'j1939_parser/j1939_frame.cpp', HERE/'socket_disabled.cpp', build/'decode.o', '-o', build/'read_ros'])
    parsed = capture([build/'read_ros', path]).splitlines()
    if len(parsed) != len(metadata):
        raise ValueError('existing reader count differs from safe YAML conversion')
    frames = []
    for n, (line, out, meta) in enumerate(zip(parsed, converted, metadata)):
        ordinal, time, cid, pgn, sa, dlc, payload = line.split()
        match = re.fullmatch(r'<0x([0-9A-F]+)> \[(\d+)\] (.*)', out)
        cid = int(cid)
        if (int(ordinal) != n or int(time) != meta['time_ns'] or cid & 0x1fffffff != int(match[1], 16) or int(dlc) != int(match[2]) or payload != match[3].replace(' ', '').lower()):
            raise ValueError(f'frame {n}: legacy reader disagrees with independent safe conversion')
        expected_pgn, expected_sa, da = unpack(cid)
        if expected_pgn != int(pgn) or expected_sa != int(sa):
            raise ValueError('PGN unpack disagreement')
        frames.append(dict(ordinal=n, id=cid & 0x1fffffff, pgn=int(pgn), sa=int(sa), da=da, dlc=int(dlc), payload=payload, extended=bool(cid & 0x80000000), **meta))
    if digest(path) != original:
        raise ValueError('capture changed while reading')
    return frames, original


def synthetic_frames():
    return json.loads((ROOT/'tests/dbc/observed_synthetic.json').read_text())['frames']


def enforce_coverage(skipped, synthetic):
    expected = Counter({'conflicting definitions: no unique exact source-address match; configure routing/default':1,
                        'no DBC definition':1, 'transport fragment; reassemble before mapping':2,
                        'short/incorrect DLC 4; expected 8':1, 'CAN FD/oversized frame unsupported':1})
    if (synthetic and skipped != expected) or (not synthetic and skipped):
        raise ValueError('incomplete coverage or unexpected synthetic skips: '+str(dict(skipped)))


def expect_rejection(program, data, status):
    result = subprocess.run([str(program)], input=data, text=True, capture_output=True)
    if result.returncode != status or result.stderr:
        raise ValueError(f'expected clean rejection {status}, got {result.returncode}: {result.stderr}')


def wrong_pgn_id(pgn):
    pf = next(pf for pf in (1,2,3) if pf != (pgn >> 8))
    return 0x18000000 | (pf << 16)


def generate(build, compiler, mappings):
    (build/'generated-c').mkdir(exist_ok=True)
    (build/'generated-cpp').mkdir(exist_ok=True)
    for m in mappings:
        stem, record = m['stem'], m['message']['name']
        brd = f'namespace: observed.{stem}\nrecord: {record}\nversion: 1\ntype: data\nfields:\n'
        for s in m['message']['signals']:
            width = next(w for w in (8, 16, 32, 64) if w >= s['width'])
            brd += f'  {s["name"]}:\n    type: {"i" if s["is_signed"] else "u"}{width}\n'
        path = build/(stem+'.brd')
        path.write_text(brd)
    configuration = build/'observed.brd'
    imports = sorted(m['stem']+'.brd' for m in mappings)
    def config(names):
        return 'configuration: observed\nimports:\n'+''.join('  - '+name+'\n' for name in names)
    configuration.write_text(config(imports))
    commands = [[compiler, '--language', 'c', '-o', build/'generated-c', configuration],
                [compiler, '-o', build/'generated-cpp', configuration],
                [compiler, '--qbs-reflective', '--emit-qbs', build/'observed.qbs', configuration]]
    for cmd in commands: run(cmd)
    files = [build/'observed.qbs', *sorted((build/'generated-c').rglob('*.*')),
             *sorted((build/'generated-cpp').rglob('*.hpp'))]
    before = {str(p.relative_to(build)): digest(p) for p in files}
    # Import order cannot change IDs, QBS, or generated code.
    configuration.write_text(config(list(reversed(imports))))
    for cmd in commands: run(cmd)
    if before != {str(p.relative_to(build)): digest(p) for p in files}:
        raise ValueError('nondeterministic configuration generation/import-order dependence')
    configuration.write_text(config(imports))
    ids = []
    for m in mappings:
        header = (build/'generated-cpp/observed'/(m['stem']+'.generated.hpp')).read_text()
        found = set(re.findall(r'\.record_id = (\d+)U', header))
        if len(found) != 1: raise ValueError('cannot establish generated record identity')
        m['record_id'] = int(found.pop()); ids.append(m['record_id'])
    if len(set(ids)) != len(ids): raise ValueError('configuration contains duplicate record IDs')
    # Full interpretation hash, including exact decimals, units, sources, and policies.
    contract = dict(version=3, qbs_sha256=digest(build/'observed.qbs'),
                    mappings=[dict(key=m['key'], record_id=m['record_id']) for m in mappings])
    contract_hash = hashlib.sha256(canonical(contract).encode()).hexdigest()
    for m in mappings: m['configuration_sha256'] = contract_hash


def native_sources(build, mappings):
    producer = ['#include "j1939_db.h"', '#include <stdio.h>', '#include <stdlib.h>', '#include <inttypes.h>', '#include <string.h>']
    consumer = ['#include <iostream>', '#include <sstream>', '#include <string>', '#include <vector>', '#include <span>', '#include <cstdint>']
    for db in sorted({m['message']['database'] for m in mappings}):
        producer.append(f'extern const j1939_db_t {db};')
    pcases, ccases = [], []
    for m in mappings:
        stem, name, key, msg = m['stem'], m['message']['name'], m['key'], m['message']
        ct = 'observed_'+stem+'_'+name
        cpp = 'observed::'+stem+'::'+name
        producer.append(f'#include "observed/{stem}.generated.h"')
        consumer.append(f'#include "observed/{stem}.generated.hpp"')
        guard = f'if(pgn!={msg["pgn"]}u || n!={msg["dlc"]}u) return 6;'
        if m['source_specific']:
            guard += 'if('+ ' && '.join(f'sa!={a["sa"]}u' for a in m['aliases']) + ')return 7;'
        body = f'if(!strcmp(key,"{key}")){{{guard}const j1939_db_t *db=&{msg["database"]};const j1939_message_t *msg=j1939_db_find(db,id,1);if(!msg)return 5;{ct}_t v; {ct}_init(&v);'
        for s in msg['signals']:
            width = next(w for w in (8, 16, 32, 64) if w >= s['width'])
            cast = ('int' if s['is_signed'] else 'uint')+str(width)+'_t'
            conversion = f'signed_raw(raw,{s["width"]})' if s['is_signed'] else 'raw'
            body += f'{{const j1939_signal_t *s=j1939_signal_by_name(db,msg,"{s["name"]}");if(!s)return 8;int a=j1939_signal_active(db,msg,payload,(uint16_t)n,s);if(a<0)return 9;if(a){{if(j1939_signal_raw(payload,(uint16_t)n,s,&raw))return 10;v.has_{s["name"]}=true;v.{s["name"]}=({cast})({conversion});}}}}'
        body += f'uint8_t out[4096];{ct}_encode_result_t encoded={ct}_encode(&v,out,sizeof(out));if(encoded.status)return 11;printf("%s %s{m["configuration_sha256"]}",key,key);for(size_t i=0;i<encoded.bytes_written;++i)printf("%02x",out[i]);'
        for s in msg['signals']:
            body += f'if(v.has_{s["name"]}){{const j1939_signal_t *s=j1939_signal_by_name(db,msg,"{s["name"]}");if(j1939_signal_raw(payload,(uint16_t)n,s,&raw))return 12;printf(" %.17g",j1939_signal_phys(s,raw));}}else printf(" x");'
        body += 'puts("");continue;}'
        pcases.append(body)
        cb = f'if(key=="{key}"){{if(hex.size()<128 || hex.substr(0,64)!=key || hex.substr(64,64)!="{m["configuration_sha256"]}")return 16;auto v=observed::{stem}::decode_{name}(bytes.subspan(64));if(!v)return 3;std::cout<<key;'
        for s in msg['signals']:
            cb += f'if(v->{s["name"]}())std::cout<<" "<<static_cast<{"int64_t" if s["is_signed"] else "uint64_t"}>(*v->{s["name"]}());else std::cout<<" x";'
        cb += 'std::cout<<"\\n";continue;}'
        ccases.append(cb)
    producer.append('''static int64_t signed_raw(uint64_t raw,unsigned width){uint64_t sign=UINT64_C(1)<<(width-1);return raw&sign?-1-(int64_t)(~raw&(sign-1)):(int64_t)raw;}
int main(void){char line[1024],key[65],hex[129],extra;unsigned id;while(fgets(line,sizeof(line),stdin)){if(sscanf(line,"%64s %u %128s %c",key,&id,hex,&extra)!=3)return 2;if(id>0x1fffffffU)return 15;size_t n=strlen(hex)/2;if(strlen(hex)%2||n>8)return 3;uint8_t payload[8];for(size_t i=0;i<n;++i){unsigned b;char pair[3]={hex[2*i],hex[2*i+1],0};char *end; b=(unsigned)strtoul(pair,&end,16);if(*end)return 4;payload[i]=(uint8_t)b;}uint32_t pgn;uint8_t sa;j1939_id_unpack(id,1,&pgn,&sa,NULL,NULL);uint64_t raw;
'''+''.join(pcases)+'''return 13;}return ferror(stdin)?14:0;}''')
    consumer.append('''int main(){std::string key,hex;while(std::cin>>key>>hex){if(hex.size()%2)return 2;std::vector<std::byte> data;for(size_t i=0;i<hex.size();i+=2){auto pair=hex.substr(i,2);size_t used=0;unsigned value;try{value=std::stoul(pair,&used,16);}catch(...){return 2;}if(used!=2)return 2;data.push_back(static_cast<std::byte>(value));}auto bytes=std::span<const std::byte>(data);
'''+''.join(ccases)+'''return 4;}return 0;}''')
    (build/'producer.c').write_text('\n'.join(producer)+'\n')
    (build/'consumer.cpp').write_text('\n'.join(consumer)+'\n')


def verify_bundle(build):
    manifest = json.loads((build/'manifest.json').read_text())
    if manifest['format'] != 'j1939.observed.mapping' or manifest['version'] != 3:
        raise ValueError('unsupported mapping contract version')
    for name, expected in manifest['artifacts'].items():
        p = (build/name).resolve()
        if not p.is_relative_to(build.resolve()) or digest(p) != expected:
            raise ValueError('mapping/schema artifact mismatch: '+name)
    return manifest


def pin_coverage(build, report):
    """Bind reported results as well as encoded data; consistency, not authenticity."""
    manifest = verify_bundle(build)
    save(build/'coverage.json', report)
    manifest['artifacts']['coverage.json'] = digest(build/'coverage.json')
    save(build/'manifest.json', manifest)
    verify_bundle(build)


def validate_stream(build, mappings, selected):
    # One input/output row per observation, not sampling or payload deduplication.
    inputs = ''.join(f'{m["key"]} {f["id"]} {f["payload"]}\n' for m, f in selected)
    (build/'selected-input.tsv').write_text(inputs)
    produced = capture([build/'producer'], input=inputs).splitlines()
    if len(produced) != len(selected):
        raise ValueError('producer dropped observations')
    (build/'producer-output.tsv').write_text('\n'.join(produced)+'\n')
    decode_input = ''.join(' '.join(line.split()[:2])+'\n' for line in produced)
    (build/'records.tsv').write_text(decode_input)
    decoded = capture([build/'consumer'], input=decode_input).splitlines()
    (build/'decoded-output.tsv').write_text('\n'.join(decoded)+'\n')
    if len(decoded) != len(selected):
        raise ValueError('consumer dropped observations')
    fields = Counter()
    for (mapping, frame), enc, dec in zip(selected, produced, decoded):
        e, d = enc.split(), dec.split()
        signals, payload = mapping['message']['signals'], bytes.fromhex(frame['payload'])
        if e[0] != mapping['key'] or d[0] != mapping['key'] or len(e) != 2+len(signals) or len(d) != 1+len(signals):
            raise ValueError('mapping key/field count mismatch')
        for s, phy, raw in zip(signals, e[2:], d[1:]):
            tag = mapping['key']+':'+s['name']
            if not active(payload, s, signals):
                if raw != 'x' or phy != 'x':
                    raise ValueError('inactive mux signal was encoded')
                fields['inactive'] += 1
                continue
            expected = raw_value(payload, s)
            if int(raw) != expected:
                raise ValueError(f'{frame["ordinal"]} {tag}: raw mismatch {raw} != {expected}')
            with localcontext() as ctx:
                ctx.prec = 96
                exact = Decimal(expected)*Decimal(s['scale'])+Decimal(s['offset'])
            got, want = float(phy), float(exact)
            # Binary64 scale conversion/product/addition rounding, including cancellation.
            scale, offset = float(s['scale']), float(s['offset'])
            product = float(expected)*scale
            tolerance = 4*(math.ulp(want)+math.ulp(product)+abs(expected)*math.ulp(scale)+math.ulp(offset))
            if not math.isfinite(got) or abs(got-want) > tolerance:
                raise ValueError(f'{frame["ordinal"]} {tag}: physical mismatch {got} vs {exact}')
            fields['raw'] += 1
            fields['physical_arithmetic'] += 1
            fields[tag] += 1
    negative = Counter()
    for mapping in mappings:
        f = next(f for m, f in selected if m['key'] == mapping['key'])
        for n in range(mapping['message']['dlc']):
            # Empty payload token still must be rejected (never passed to unchecked accessors).
            bad = f'{mapping["key"]} {f["id"]} {f["payload"][:2*n]}\n'
            expect_rejection(build/'producer', bad, 2 if n==0 else 6)
            negative['short_frames'] += 1
        if mapping['source_specific']:
            allowed={a['sa'] for a in mapping['aliases']}
            wrong=next(sa for sa in range(256) if sa not in allowed)
            wrong_id=(f['id'] & ~255)|wrong
            expect_rejection(build/'producer',f'{mapping["key"]} {wrong_id} {f["payload"]}\n',7)
            negative['wrong_source'] += 1
        line = next(line for line in produced if line.startswith(mapping['key']+' '))
        key, encoded = line.split()[:2]
        for badhex in (encoded[:-2], encoded[:128]+'00'+encoded[130:]):
            expect_rejection(build/'consumer',key+' '+badhex+'\n',3)
            negative['malformed_brf'] += 1
    first = produced[0].split()
    for program, bad in [('consumer', '0'*64+' '+first[1]+'\n'), ('producer', '0'*64+' '+str(selected[0][1]['id'])+' '+selected[0][1]['payload']+'\n')]:
        expect_rejection(build/program,bad,4 if program=='consumer' else 13)
        negative['mapping_key'] += 1
    for mapping in mappings:
        f=next(f for m,f in selected if m['key']==mapping['key'])
        wrong_id=wrong_pgn_id(mapping['message']['pgn'])
        expect_rejection(build/'producer',f'{mapping["key"]} {wrong_id} {f["payload"]}\n',6)
        negative['wrong_pgn'] += 1
    # Every distinct pair, including identical scalar layouts and unit/scale-only differences.
    samples={line.split()[0]:line.split()[1] for line in produced}
    for source in mappings:
        encoded=samples[source['key']]
        for target in mappings:
            if source is target: continue
            expect_rejection(build/'consumer',target['key']+' '+encoded+'\n',16)
            negative['cross_mapping'] += 1
            # Even if envelope is relabelled, the joint configuration's record ID rejects BRF.
            relabelled=target['key']+target['configuration_sha256']+encoded[128:]
            expect_rejection(build/'consumer',target['key']+' '+relabelled+'\n',3)
            negative['cross_record_id'] += 1
        changed=encoded[:64]+('0' if encoded[64]!='0' else '1')+encoded[65:]
        expect_rejection(build/'consumer',source['key']+' '+changed+'\n',16)
        negative['configuration_identity'] += 1
    for target, tag in [(build/'observed.qbs', 'schema_artifact'), (build/'mapping.json', 'mapping_artifact'), (build/'context.json', 'context_artifact')]:
        original = target.read_bytes()
        try:
            if tag=='context_artifact':
                rows=json.loads(original);rows[0],rows[1]=rows[1],rows[0];save(target,rows)
            else:
                target.write_bytes(original+b'corruption')
            try:
                verify_bundle(build)
            except ValueError:
                negative[tag] += 1
            else:
                raise ValueError('modified artifact accepted: '+target.name)
        finally:
            target.write_bytes(original)
    manifest_path=build/'manifest.json'
    original=manifest_path.read_bytes()
    try:
        altered=json.loads(original);altered['version']=999;save(manifest_path,altered)
        try:
            verify_bundle(build)
        except ValueError:
            negative['mapping_version'] += 1
        else:
            raise ValueError('unknown mapping version accepted')
    finally:
        manifest_path.write_bytes(original)
    return dict(fields), dict(negative)


def quarry_provenance(compiler, build):
    def git(*args):
        result = subprocess.run(['git','-C',str(QUARRY),*args],text=True,capture_output=True)
        return result.stdout.strip() if result.returncode == 0 else None
    revision = git('rev-parse','HEAD')
    changed = git('diff','--name-only','HEAD','--')
    cache = build/'CMakeCache.txt'
    match = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$',cache.read_text(),re.M) if cache.is_file() else None
    return dict(source_path=str(QUARRY), source_revision=revision,
                tracked_changes=changed.splitlines() if changed else [],
                tracked_clean=(changed == '') if revision else None,
                runtime_sha256=digest(QUARRY/'runtime_c/typed_record.c'),
                cpp_backend_sha256=digest(QUARRY/'compiler/backend/backend.cpp'),
                compiler_path=str(compiler), compiler_sha256=digest(compiler),
                build_path=str(build.resolve()), recorded_build_source=match[1] if match else None,
                note='selected inputs, not authentication or build attestation; rebuild from committed sources for baseline attribution')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', nargs='?', type=Path)
    ap.add_argument('--synthetic', action='store_true')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--dbc', type=Path)
    ap.add_argument('--supplemental-dbc',type=Path,action='append')
    ap.add_argument('--quarry-build', type=Path, default=QUARRY/'build/debug')
    ap.add_argument('--compiler', type=Path)
    ap.add_argument('--private-profile', type=Path, help='explicit out-of-tree reviewed input fingerprints; not authentication')
    ap.add_argument('--capture-reader-sources', type=Path, help='external directory containing reviewed ros_stream.cpp/.h')
    ap.add_argument('--sanitize', action='store_true')
    ap.add_argument('--tools-from', type=Path)
    args = ap.parse_args()
    global REVIEWED_VENDOR, REVIEWED_CAPTURE
    if not args.synthetic:
        if not all((args.capture, args.dbc, args.private_profile, args.capture_reader_sources)):
            ap.error('capture mode requires capture, --dbc, --private-profile, and --capture-reader-sources; no private defaults')
        REVIEWED_VENDOR, REVIEWED_CAPTURE = load_private_profile(args.private_profile)

    b = args.output.expanduser().resolve()
    if b.is_relative_to(ROOT) or b.is_relative_to(QUARRY):
        raise ValueError('keep generated/captured artifacts outside both repositories')
    b.mkdir(parents=True, exist_ok=True)
    dbc = (args.dbc or ROOT/'tests/dbc/observed_synthetic.dbc').resolve()
    supplements=args.supplemental_dbc or []
    dbcs=[dbc,*[p.expanduser().resolve() for p in supplements]]
    if len({p.name for p in dbcs})!=len(dbcs):raise ValueError('duplicate logical DBC source names')
    compiler = (args.compiler or args.quarry_build/'tools/quarry-schema-compiler').resolve()
    if not compiler.is_file():
        raise ValueError('build Quarry compiler first or supply --compiler')
    provenance = quarry_provenance(compiler, args.quarry_build)
    print('QUARRY_PROVENANCE '+json.dumps(provenance,sort_keys=True),flush=True)
    selection = dict(runner=digest(Path(__file__)), fixture=digest(ROOT/'tests/dbc/observed_synthetic.json') if args.synthetic else None, dbc={p.name:digest(p) for p in dbcs}, capture='synthetic-v1' if args.synthetic else digest(args.capture.expanduser()), compiler=digest(compiler))
    if (b/'input-selection.json').exists() and json.loads((b/'input-selection.json').read_text()) != selection:
        raise ValueError('input selection changed: use a fresh --output directory')
    save(b/'input-selection.json', selection)
    includes, flags = compile_tools(b, args.sanitize, args.tools_from)
    messages=[]
    for index,source in enumerate(dbcs):
        models=project(b,source,b/'sources'/f'db_{index}')
        for m in models:
            m.update(source=source.name,source_sha256=digest(source),database=f'observed_{index}_db',source_index=index)
            if index==0 and m['pgn']==0xea00:m['request_default']=True
            if index>0 and m['pgn']>=0xff00:m['declared_sources_only']=True
        messages.extend(models)
    by_pgn = defaultdict(list)
    for m in messages:
        if m['extended']:
            by_pgn[m['pgn']].append(m)
    if args.synthetic:
        all_signals=[s for m in messages for s in m['signals']]
        if not any(s['spn']==0 for s in all_signals) or not any(any(e['raw']==2**64-1 for e in s['enums']) for s in all_signals):
            raise ValueError('synthetic enum/SPN metadata was lost')
        frames, capture_hash = synthetic_frames(), None
    else:
        frames, capture_hash = read_capture(b, args.capture.expanduser().resolve(), args.capture_reader_sources, includes, flags)
    mappings, chosen, inventory = {}, [], defaultdict(lambda: dict(count=0, sources=Counter(), destinations=Counter(), statuses=Counter(), reasons=Counter()))
    for frame in frames:
        item = inventory[frame['pgn']]
        item['kind']='request_control' if frame['pgn']==0xea00 else 'transport' if frame['pgn'] in (0xec00,0xeb00) else 'application'
        item['count'] += 1
        item['sources'][str(frame['sa'])] += 1
        item['destinations'][str(frame['da'])] += 1
        try:
            msg, aliases, policy = select(by_pgn[frame['pgn']], frame)
        except ValueError as exc:
            reason = str(exc)
            status = 'transport' if frame['pgn'] in (0xec00, 0xeb00) else 'unknown' if not by_pgn[frame['pgn']] else 'unsupported'
            item['statuses'][status] += 1
            item['reasons'][reason] += 1
            continue
        source_specific=(len({signature(a) for a in by_pgn[frame['pgn']]})>1 or any(a.get('declared_sources_only') for a in aliases)) and not msg.get('request_default',False)
        identity = dict(format='j1939.observed.mapping', version=3, dbc_sha256=msg['source_sha256'], pgn=frame['pgn'], definition=signature(msg), policy=policy, applicable_sources=sorted({a['sa'] for a in aliases}) if source_specific else None)
        key = hashlib.sha256(canonical(identity).encode()).hexdigest()
        if key not in mappings:
            mappings[key] = dict(key=key, stem=f'pgn_{frame["pgn"]:05x}_{key[:12]}', message=msg, aliases=aliases, source_specific=source_specific, policy=policy)
        chosen.append((mappings[key], frame))
        item['statuses']['supported'] += 1
    mappings = sorted(mappings.values(), key=lambda m: m['key'])
    inv = [dict(pgn=pgn, dbc_definitions=[dict(id=m['id'],name=m['name'],sa=m['sa'],source=m['source'],source_sha256=m['source_sha256']) for m in by_pgn[pgn]], **item) for pgn, item in sorted(inventory.items())]
    save(b/'inventory.json', inv)
    save(b/'capture-summary.json',dict(frames=len(frames),extended=sum(f['extended'] for f in frames),dlcs=dict(Counter(str(f['dlc']) for f in frames)),flags=dict(Counter(str(f.get('flag')) for f in frames)),buses=dict(Counter(str(f.get('bus')) for f in frames)),statuses=dict(Counter(str(f.get('status')) for f in frames)),time_min=min((f['time_ns'] for f in frames if f.get('time_ns') is not None),default=None),time_max=max((f['time_ns'] for f in frames if f.get('time_ns') is not None),default=None),backward_timestamp_steps=sum(a.get('time_ns') is not None and b_.get('time_ns') is not None and a['time_ns']>b_['time_ns'] for a,b_ in zip(frames,frames[1:])),transport_frames=sum(f['pgn'] in (0xec00,0xeb00) for f in frames)))
    enforce_coverage(Counter({reason:sum(item['reasons'].get(reason,0) for item in inv) for reason in {r for item in inv for r in item['reasons']}}), args.synthetic)
    if not mappings:
        raise ValueError('no complete supported mappings; see inventory.json')
    generate(b, compiler, mappings)
    native_sources(b, mappings)
    cflags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic', *flags, '-I'+str(ROOT/'j1939_parser'), '-I'+str(b), '-I'+str(b/'generated-c'), '-I'+str(QUARRY/'include'), '-I'+str(args.quarry_build/'generated')]
    for label, source in [('decode', ROOT/'j1939_parser/j1939_db.c'), ('typed', QUARRY/'runtime_c/typed_record.c')]:
        run(['cc', *cflags, '-c', source, '-o', b/(label+'.o')])
    database_objects=[]
    for index in sorted({m['message']['source_index'] for m in mappings}):
        obj=b/f'database_{index}.o';database_objects.append(obj)
        run(['cc',*cflags,f'-Dobserved_db=observed_{index}_db','-c',b/'sources'/f'db_{index}'/'database.c','-o',obj])
    run(['cc', *cflags, b/'producer.c', b/'decode.o', *database_objects, b/'typed.o', '-o', b/'producer'])
    run(['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', *flags, '-I'+str(b/'generated-cpp'), '-I'+str(QUARRY/'include'), '-I'+str(args.quarry_build/'generated'), b/'consumer.cpp', '-o', b/'consumer'])
    source_metadata=[dict(source=source.name,line=i,text=line) for source in dbcs for i,line in enumerate(source.read_text().splitlines(),1) if line.startswith(('CM_', 'BA_', 'BA_DEF_', 'BA_DEF_DEF_', 'VAL_'))]
    metadata = dict(source_metadata=source_metadata, format='j1939.observed.mapping', version=3, source_sha256={p.name:digest(p) for p in dbcs}, mappings=mappings, timestamp_policy='capture secs/nsecs retained verbatim in sidecar; epoch/clock not inferred; synthetic timestamps absent', quality_policy='unknown; physical checks are arithmetic only; no sentinel inference', record_policy='one record per observation; checked 32-byte mapping + 32-byte configuration digest envelope preceding unchanged BRF; no batching')
    save(b/'mapping.json', metadata)
    save(b/'context.json', [dict(mapping=m['key'], quality='unknown', **f) for m, f in chosen])
    artifact_paths = [b/'producer', b/'consumer', b/'mapping.json', b/'context.json', *b.glob('sources/**/*.c'), *b.glob('sources/**/*.h'), b/'producer.c', b/'consumer.cpp', *b.glob('*.brd'), *b.glob('*.qbs'), *b.glob('generated-c/**/*.*'), *b.glob('generated-cpp/**/*.hpp')]
    save(b/'manifest.json', dict(format='j1939.observed.mapping', version=3, quarry_provenance=provenance, capture_sha256=capture_hash, compiler_sha256=digest(compiler), j1939_codec_sha256=digest(ROOT/'j1939_parser/j1939_db.c'), quarry_runtime_sha256=digest(QUARRY/'runtime_c/typed_record.c'), artifacts={str(p.relative_to(b)): digest(p) for p in artifact_paths if p.is_file()}))
    verify_bundle(b)
    fields, negatives = validate_stream(b, mappings, chosen)
    manifest=verify_bundle(b)
    manifest['artifacts'].update({name:digest(b/name) for name in ('records.tsv','selected-input.tsv','producer-output.tsv','decoded-output.tsv')})
    save(b/'manifest.json',manifest)
    verify_bundle(b)
    by_schema = [dict(pgn=m['message']['pgn'], key=m['key'], name=m['message']['name'], source_specific=m['source_specific'], frames=sum(1 for chosen_m,_ in chosen if chosen_m['key']==m['key']), defined_signals=len(m['message']['signals']), active_signal_checks=sum(fields.get(m['key']+':'+s['name'],0) for s in m['message']['signals']), signals_exercised=sum(fields.get(m['key']+':'+s['name'],0)>0 for s in m['message']['signals'])) for m in mappings]
    report = dict(by_schema=by_schema, total_frames=len(frames), observed_pgns=len(inventory), supported_frames=len(chosen), supported_pgns=len({m['message']['pgn'] for m in mappings}), schemas=len(mappings), unique_supported_payloads=len({(m['key'],f['payload']) for m,f in chosen}), defined_signals=sum(len(m['message']['signals']) for m in mappings), signal_checks=fields, negative_checks=negatives, skipped=dict(Counter(reason for item in inv for reason,count in item['reasons'].items() for _ in range(count))), capture_sha256=capture_hash, producer='generated C + J1939 C codec; caller-owned structs/buffers; no codec heap', consumer='separate owning C++; no J1939 parser/decoder linked')
    pin_coverage(b, report)
    # Other available DBCs are inventory evidence, never implicitly merged or preferred.
    available = sorted(set(dbcs) | set(ROOT.glob('tests/dbc/model_v1/*.dbc')) | {ROOT/'tests/dbc/observed_synthetic.dbc'})
    availability = []
    for source in available:
        result = subprocess.run([str(b/'headers'),str(source)], text=True, capture_output=True)
        matches = defaultdict(list)
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                pgn, cid, name = line.split()
                if (int(cid) & 0x80000000) and not (int(cid) & 0x60000000) and int(pgn) in inventory:
                    matches[pgn].append(dict(id=int(cid),name=name))
        availability.append(dict(source=str(source),sha256=digest(source),header_inventory_only=True,unverified_diagnostic=True,error=result.stderr if result.returncode else None,observed_matches=dict(matches)))
    save(b/'available-dbcs.json', availability)
    print(json.dumps({k:v for k,v in report.items() if k not in ('signal_checks','by_schema')},sort_keys=True))
    print('PASS: complete capture verified' if not args.synthetic else 'PASS: synthetic supported observations verified; exact expected skips enforced', 'artifacts:',b)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError, OSError) as exc:
        print('ERROR:',exc,file=sys.stderr)
        sys.exit(1)
