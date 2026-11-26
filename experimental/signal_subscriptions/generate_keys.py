#!/usr/bin/env python3
"""Host-only keys from strict model + actual generated C metadata, never a DBC parser."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import struct
import tempfile
from google.protobuf import json_format, text_format
import dbc_model_v1_pb2 as pb
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def widths(text):
    try: bits=tuple(map(int,text.split(',')))
    except ValueError as e: raise ValueError('widths must be three integers') from e
    if len(bits)!=3 or any(x<1 or x>32 for x in bits) or sum(bits)!=32:
        raise ValueError('positive domain/source/signal widths must sum to32')
    return bits

def plan(model, exported, bits, constant_prefix="SUB_ID_"):
    if not model.IsInitialized() or model.format!='j1939.dbc-model' or model.version!=1:
        raise ValueError('unsupported/incomplete host model')
    sources=[json_format.MessageToDict(s,preserving_proto_field_name=True) for s in model.sources]
    if len({s.name for s in model.sources})!=len(sources):raise ValueError('duplicate source')
    seen=set();ids=set();names=set();flat=[];messages=[]
    for m in model.messages:
        if m.dbc_id in ids:raise ValueError('duplicate message definition/CAN ID')
        ids.add(m.dbc_id)
        md=json_format.MessageToDict(m,preserving_proto_field_name=True)
        md['signals']=sorted(md['signals'],key=lambda s:s['name'])
        for item in md['signals']:
            if 'enums' in item:item['enums']=sorted(item['enums'],key=lambda e:int(e['raw']))
        messages.append(md)
        for s in m.signals:
            key=(m.source,m.dbc_id,m.name,s.name)
            if key in seen:raise ValueError('duplicate signal definition')
            seen.add(key)
            constant=constant_prefix+re.sub('[^A-Za-z0-9_]','_',m.source[:-4]+'_'+m.name+'_'+s.name).upper()
            if len(constant)>63:raise ValueError('generated identifier exceeds63 characters: '+constant)
            if constant in names:raise ValueError('generated-name collision: '+constant)
            names.add(constant)
            sd=json_format.MessageToDict(s,preserving_proto_field_name=True)
            if 'enums' in sd:sd['enums']=sorted(sd['enums'],key=lambda e:int(e['raw']))
            flat.append((key,constant,md,sd))
    if not flat:raise ValueError('empty catalog')
    if len(flat)>(1<<bits[2]):raise ValueError('signal ID space exhausted')
    indices={}
    for e in exported['entries']:
        key=(e['source'],e['dbc_id'],e['message'],e['signal'])
        if key in indices:raise ValueError('duplicate metadata definition')
        indices[key]=e
    if set(indices)!=seen:raise ValueError('host model/generated C definition mismatch')
    canonical={'generator_version':1,'assignment':'source,dbc_id,message,signal; dense zero-based',
               'model':{'format':model.format,'version':model.version,'sources':sorted(sources,key=lambda s:s['name']),
                        'messages':sorted(messages,key=lambda m:(m['source'],m['dbc_id'],m['name']))},
               'metadata_hash':exported['metadata_hash']}
    canonical['entries']=[[indices[key]['message_definition'],indices[key]['signal_definition']] for key,_,_,_ in sorted(flat,key=lambda e:e[0])]
    entry_hash=14695981039346656037
    for value in [len(flat)]+[v for pair in canonical['entries'] for v in pair]:
        for byte in value.to_bytes(8,'little'):entry_hash=((entry_hash^byte)*1099511628211)&((1<<64)-1)
    sha=hashlib.sha256(json.dumps(canonical,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()).hexdigest()
    signals=[]
    for i,(key,constant,md,sd) in enumerate(sorted(flat,key=lambda e:e[0])):
        e=indices[key]
        m=next(m for m in model.messages if m.dbc_id==key[1])
        s=next(s for s in m.signals if s.name==key[3])
        src=next(src for src in model.sources if src.name==m.source)
        is_mux=s.HasField('multiplex')
        role=s.multiplex.role if is_mux else None
        numbers=[s.scale,s.offset,s.minimum,s.maximum]
        if s.source!=m.source:raise ValueError('signal source mismatch')
        expected={'original_can_id':m.can_id,'extended':m.extended,'pgn':m.pgn if m.HasField('pgn') else None,
            'source_address':m.source_address if m.HasField('source_address') else None,'start_bit':s.start_bit,'width':s.width,'flags':int(s.is_signed)+2*int(s.byte_order==0)+(4 if role==0 else 8 if role==1 else 0),
            'mux':s.multiplex.value if role==1 else 0,'unit':s.unit,'numbers':numbers,
            'bits':[struct.pack('>d',float(value)).hex() for value in numbers],
            'enums':[[str(v.raw),v.label] for v in sorted(s.enums,key=lambda v:v.raw)],
            'spn':s.spn if s.HasField('spn') else None,'line':s.line,'message_dlc':m.dlc,'message_line':m.line,
            'message_spn':m.spn if m.HasField('spn') else None,'default':m.j1939_default if m.HasField('j1939_default') else None,
            'source_bytes':str(src.byte_length),'source_fnv':src.fnv1a64,
            'source_version':src.dbc_version if src.HasField('dbc_version') else None,
            'switch_name':s.multiplex.switch_name if role==1 else None}
        if e['semantic']!=expected:raise ValueError('host model/generated C interpretation mismatch: '+str(key))
        signals.append({'id':i,'constant':constant,'source':key[0],'dbc_id':key[1],'message':key[2],
                        'message_definition':e['message_definition'],'signal_definition':e['signal_definition'],
                        'signal':sd,'message_context':{k:v for k,v in md.items() if k!='signals'}})
    return {'version':1,'catalog_sha256':sha,'metadata_hash':exported['metadata_hash'],
            'entry_hash':f'{entry_hash:016x}','layout':list(bits),'assignment':canonical['assignment'],'sources':canonical['model']['sources'],'signals':signals}

def export_database(database,symbol,cc):
    if not re.fullmatch('[A-Za-z_][A-Za-z0-9_]*',symbol):raise ValueError('invalid metadata C symbol')
    with tempfile.TemporaryDirectory(prefix='keys-probe-') as tmp:
        tmp=Path(tmp)
        command=cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),
                    '-DMETADATA_SYMBOL='+symbol,str(HERE/'catalog_probe.c'),str(database),str(HERE/'j1939_signal_keys.c'),str(HERE/'j1939_subscriptions.c'),
                    str(ROOT/'j1939_parser/j1939_db.c'),'-o',str(tmp/'probe')]
        subprocess.run(command,check=True)
        return json.loads(subprocess.check_output([str(tmp/'probe')],text=True))

def catalog_names(name):
    # Canonical spelling makes uppercasing injective. ID is a delimiter, never
    # part of a catalog namespace, so different catalog ID prefixes cannot nest.
    if not re.fullmatch('[a-z][a-z0-9]*(?:_[a-z0-9]+)*',name):
        raise ValueError('catalog namespace must use canonical lowercase snake case: '+name)
    if 'id' in name.split('_') or name in ('sub','sub_generated'):
        raise ValueError('reserved catalog namespace (id delimiter or legacy SUB macros): '+name)
    custom=name!='generated_keys'
    names={'stamp':name.upper()+'_CATALOG_SHA256' if custom else 'SUB_GENERATED_CATALOG_SHA256',
           'layout':name.upper()+'_LAYOUT' if custom else 'SUB_GENERATED_LAYOUT',
           'root':name+'_catalog','guard':'J1939_GENERATED_PACKED_CATALOG_'+name.upper()+'_H'}
    for value in names.values():
        if len(value)>63:raise ValueError('generated identifier exceeds63 characters: '+value)
    return names,name.upper()+'_ID_' if custom else 'SUB_ID_'

def check_metadata_symbol(symbol,names):
    if symbol in names:raise ValueError('metadata symbol collides with generated public identifier: '+symbol)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--model',required=True);ap.add_argument('--database',required=True)
    ap.add_argument('--metadata-symbol',required=True);ap.add_argument('--out-prefix',required=True)
    ap.add_argument('--bits',default='8,8,16');ap.add_argument('--catalog-name',default='generated_keys');args=ap.parse_args()
    try:
        names,id_prefix=catalog_names(args.catalog_name)
        stamp,layout_macro,root,guard=(names[k] for k in ('stamp','layout','root','guard'))
        check_metadata_symbol(args.metadata_symbol,names.values())
        bits=widths(args.bits);model=text_format.Parse(Path(args.model).read_text(),pb.Model())
        exported=export_database(Path(args.database).resolve(),args.metadata_symbol,shlex.split(os.environ.get('CC','cc')))
        manifest=plan(model,exported,bits,id_prefix)
        public=list(names.values())+[e['constant'] for e in manifest['signals']]
        if len(public)!=len(set(public)):raise ValueError('generated public identifier collision')
        check_metadata_symbol(args.metadata_symbol,public)
        prefix=Path(args.out_prefix)
        if not re.fullmatch('[A-Za-z_][A-Za-z0-9_]*',prefix.name):raise ValueError('output prefix needs a simple C-style basename without suffix')
        prefix.parent.mkdir(parents=True,exist_ok=True)
        header='/* Experimental generated IDs; scoped by catalog SHA256 and binding identity. */\n#ifndef '+guard+'\n#define '+guard+'\n#include "j1939_signal_keys.h"\n'
        for entry in manifest['signals']:header+=f'#define {entry["constant"]} UINT32_C({entry["id"]})\n'
        header+='#define '+stamp+' { '+', '.join('0x'+manifest['catalog_sha256'][i:i+2] for i in range(0,64,2))+' }\n'
        header+='#define '+layout_macro+' { '+', '.join(map(str,bits))+' }\nextern const j1939_key_catalog_t '+root+';\n#endif\n'
        source='#include "'+prefix.name+'.h"\nextern const j1939_db_metadata_t '+args.metadata_symbol+';\nstatic const j1939_key_entry_t entries[] = {\n'
        for e in manifest['signals']:source+=f' {{ {e["message_definition"]}u, {e["signal_definition"]}u }},\n'
        source+='};\nconst j1939_key_catalog_t '+root+' = { &'+args.metadata_symbol+', entries, '+str(len(manifest['signals']))+'u, 1u, UINT64_C(0x'+manifest['metadata_hash']+'), UINT64_C(0x'+manifest['entry_hash']+'), { '+', '.join('0x'+manifest['catalog_sha256'][i:i+2] for i in range(0,64,2))+' } };\n'
        # All validation precedes writes; OS failures still are not an atomic multi-file transaction.
        prefix.with_suffix('.h').write_text(header);prefix.with_suffix('.c').write_text(source)
        prefix.with_suffix('.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
    except (ValueError,OSError,text_format.ParseError,subprocess.CalledProcessError) as e:ap.exit(1,str(e)+'\n')
if __name__=='__main__':main()
