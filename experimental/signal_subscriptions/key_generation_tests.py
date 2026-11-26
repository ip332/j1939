#!/usr/bin/env python3
"""Strict-model/actual-C generation contracts; synthetic inputs only."""
import os
import shlex
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from google.protobuf import text_format
import dbc_model_v1_pb2 as pb
sys.dont_write_bytecode=True
import generate_keys as generator
COMPILER=Path(sys.argv.pop(1)).resolve();BUILD=Path(sys.argv.pop(1)).resolve()
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
class GenerationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.model=text_format.Parse((BUILD/'model.textproto').read_text(),pb.Model())
    def cli(self,model=None,database=None,bits='8,8,16',symbol='subscriptions_metadata',prefix=None,catalog='generated_keys'):
        path=self.root/'input.textproto';path.write_text(text_format.MessageToString(model or self.model))
        out=prefix or self.root/'signal_keys'
        p=subprocess.run([sys.executable,str(HERE/'generate_keys.py'),'--model',str(path),'--database',str(database or BUILD/'subscriptions.c'),
             '--metadata-symbol',symbol,'--out-prefix',str(out),'--bits',bits,'--catalog-name',catalog],capture_output=True,text=True)
        return p,out
    def reject(self,model=None,**kwargs):
        for ext in ['c','h','json']:(self.root/('signal_keys.'+ext)).write_text('sentinel')
        p,out=self.cli(model,**kwargs);self.assertEqual(p.returncode,1,p.stderr)
        for ext in ['c','h','json']:self.assertEqual(out.with_suffix('.'+ext).read_text(),'sentinel')
        return p.stderr
    def test_runtime_header_guard_names_compile(self):
        cc=shlex.split(os.environ.get('CC','cc'))
        headers=[]
        model,database=self.make('BO_ 2566848779 X: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] "" ECU\n',source_name="a.dbc")
        for name in ['j1939_db','experimental_j1939_signal_keys','j1939_key_queue','generated_keys']:
            out=self.root/name
            p,out=self.cli(model,database=database,symbol="custom_metadata",prefix=out,catalog=name);self.assertEqual(p.returncode,0,p.stderr)
            command=cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),'-c',str(out.with_suffix('.c')),
                '-o',str(out.with_suffix('.o'))]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            headers.append('#include "'+name+'.h"')
        smoke=self.root/'combined.c'
        smoke.write_text('\n'.join(headers)+'\nint main(void){return 0;}\n')
        result=subprocess.run(cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic',
            '-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),'-fsyntax-only',str(smoke)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_catalog_namespace_rejections_preserve_outputs(self):
        for name in ['Foo','FOO','foo-bar','foo.bar','_foo','foo_','foo__bar','9foo','foo id','id','foo_id_bar','sub','sub_generated']:
            with self.subTest(name=name):
                diagnostic=self.reject(catalog=name)
                self.assertIn('catalog namespace',diagnostic)
                out=self.root/'absent'
                p,_=self.cli(prefix=out,catalog=name)
                self.assertEqual(p.returncode,1,p.stderr)
                for ext in ['h','c','json']:self.assertFalse(out.with_suffix('.'+ext).exists())
    def test_public_names_and_metadata_collisions(self):
        for name in ['foo','foo2','foo_bar','generated_keys']:
            names,prefix=generator.catalog_names(name)
            self.assertEqual(len(set(names.values())),4)
            for symbol in names.values():
                self.assertIn('metadata symbol collides',self.reject(catalog=name,symbol=symbol))
        self.assertIn('exceeds63',self.reject(catalog='a'*64))
        model,database=self.make('BO_ 2566848779 X: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] "" ECU\n',source_name='a.dbc')
        renamed=self.root/'renamed.c'
        renamed.write_text(database.read_text().replace('custom_metadata','FOO_ID_A_X_V'))
        self.assertIn('metadata symbol collides',self.reject(model,database=renamed,symbol='FOO_ID_A_X_V',catalog='foo'))
    def test_two_catalogs_compile_link_and_run(self):
        cc=shlex.split(os.environ.get('CC','cc'))
        model,database=self.make('BO_ 2566848779 X: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] "" ECU\n',source_name='a.dbc')
        sources=[];headers=[]
        for name in ['foo','foo_bar']:
            p,out=self.cli(model,database=database,symbol='custom_metadata',catalog=name,prefix=self.root/name)
            self.assertEqual(p.returncode,0,p.stderr);sources.append(str(out.with_suffix('.c')));headers.append('#include "'+name+'.h"')
        main=self.root/'main.c'
        main.write_text('\n'.join(headers)+\
            '\n#include <assert.h>\nint main(void){\n'
            'static const unsigned char a[32]=FOO_CATALOG_SHA256,b[32]=FOO_BAR_CATALOG_SHA256;\n'
            'j1939_key_layout_t l=FOO_LAYOUT,m=FOO_BAR_LAYOUT;\n'
            'assert(foo_catalog.count==1 && foo_bar_catalog.count==1);\n'
            'assert(FOO_ID_A_X_V==0 && FOO_BAR_ID_A_X_V==0);\n'
            'assert(l.signal_bits==16 && m.signal_bits==16 && a[0]==b[0]);return 0;}\n')
        command=cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined',
            '-I'+str(ROOT/'j1939_parser'),'-I'+str(HERE),str(main),*sources,str(database),'-o',str(self.root/'combined')]
        result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
        result=subprocess.run([str(self.root/'combined')],capture_output=True,text=True,
            env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        self.assertEqual(result.returncode,0,result.stderr)
        # The reported second spelling is rejected; it cannot overwrite foo's outputs.
        before={ext:(self.root/'foo').with_suffix('.'+ext).read_bytes() for ext in ['c','h','json']}
        p,out=self.cli(model,database=database,symbol='custom_metadata',catalog='Foo',prefix=self.root/'foo')
        self.assertEqual(p.returncode,1);self.assertIn('canonical lowercase',p.stderr)
        for ext,data in before.items():self.assertEqual(out.with_suffix('.'+ext).read_bytes(),data)
    def test_punctuation_sanitization_collision(self):
        model=pb.Model();model.CopyFrom(self.model)
        # Two source basenames sanitize to the same spelling; unique CAN IDs keep
        # these actual definitions distinct until generated-name validation.
        first=model.messages[0];second=model.messages[1]
        first.source='a-b.dbc';second.source='a_b.dbc';second.name=first.name
        for message in [first,second]:
            source=model.sources.add();source.CopyFrom(model.sources[0]);source.name=message.source
            for signal in message.signals:signal.source=message.source
        second.signals[0].name=first.signals[0].name
        with self.assertRaisesRegex(ValueError,'generated-name collision'):
            generator.plan(model,{'entries':[]},(8,8,16))
    def test_widths(self):
        for text in ['2,10,20','8,8,16']:self.assertEqual(sum(generator.widths(text)),32)
        for text in ['0,8,24','8,0,24','8,8,0','8,8,15','33,1,1','-1,13,20','a,8,16','32']:
            with self.assertRaises(ValueError):generator.widths(text)
    def test_deterministic_and_model_order_independent(self):
        p,a=self.cli();self.assertEqual(p.returncode,0,p.stderr)
        before={e:a.with_suffix('.'+e).read_bytes() for e in ['c','h','json']}
        reverse=pb.Model();reverse.CopyFrom(self.model)
        messages=list(reverse.messages);del reverse.messages[:]
        for old in reversed(messages):
            m=reverse.messages.add();m.CopyFrom(old);signals=list(m.signals);del m.signals[:]
            for old_s in reversed(signals):m.signals.add().CopyFrom(old_s)
        p,a=self.cli(reverse);self.assertEqual(p.returncode,0,p.stderr)
        for ext,data in before.items():self.assertEqual(a.with_suffix('.'+ext).read_bytes(),data)
    def test_exhaustion_preserves_outputs(self):self.assertIn('ID space exhausted',self.reject(bits='15,16,1'))
    def test_duplicate_message(self):
        self.model.messages.add().CopyFrom(self.model.messages[0]);self.assertIn('duplicate message',self.reject(self.model))
    def test_duplicate_signal(self):
        self.model.messages[0].signals.add().CopyFrom(self.model.messages[0].signals[0]);self.assertIn('duplicate signal',self.reject(self.model))
    def test_model_interpretation_mismatch(self):
        changed=pb.Model();changed.CopyFrom(self.model);changed.messages[0].signals[0].unit='wrong unit'
        self.assertIn('interpretation mismatch',self.reject(changed))
        changed.CopyFrom(self.model);changed.messages[0].pgn=123
        self.assertIn('interpretation mismatch',self.reject(changed))
    def test_actual_numeric_constant_mismatch(self):
        source=(BUILD/'subscriptions.c').read_text();changed=source.replace('1.0, 0.0, 0.0, 255.0','2.0, 0.0, 0.0, 255.0',1)
        self.assertNotEqual(source,changed);path=self.root/'wrong.c';path.write_text(changed)
        self.assertIn('interpretation mismatch',self.reject(database=path))
    def test_spn_not_identity(self):
        p,a=self.cli();self.assertEqual(p.returncode,0,p.stderr);data=json.loads(a.with_suffix('.json').read_text())
        repeated=[s['id'] for s in data['signals'] if s['signal'].get('spn')==100]
        self.assertEqual(len(repeated),3);self.assertEqual(len(set(repeated)),3)
        self.assertTrue(any('spn' not in s['signal'] for s in data['signals']))
        self.assertEqual(len([s for s in data['signals'] if s['signal'].get('spn')==0]),1)
    def test_layout_separate_from_catalog(self):
        p,a=self.cli();self.assertEqual(p.returncode,0,p.stderr);first=json.loads(a.with_suffix('.json').read_text())
        p,a=self.cli(bits='2,10,20');self.assertEqual(p.returncode,0,p.stderr);second=json.loads(a.with_suffix('.json').read_text())
        self.assertEqual(first['catalog_sha256'],second['catalog_sha256']);self.assertNotEqual(first['layout'],second['layout'])
    def make(self,text,source_name="subscriptions_synthetic.dbc"):
        source=self.root/source_name;source.write_text(text)
        database=self.root/'custom.c';model=self.root/'custom.textproto'
        for flags in [['-name','custom','-c_path',str(database),'-c-metadata'],['-model_path',str(model)]]:
            p=subprocess.run([str(COMPILER),'-in',str(source),*flags],capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
        return text_format.Parse(model.read_text(),pb.Model()),database
    def test_generated_name_collision(self):
        model,database=self.make('BO_ 2566848779 Collision: 8 ECU\n SG_ Value : 0|8@1+ (1,0) [0|255] "" ECU\n SG_ VALUE : 8|8@1+ (1,0) [0|255] "" ECU\n')
        self.assertIn('generated-name collision',self.reject(model,database=database,symbol='custom_metadata'))
    def test_identifier_length_rejects(self):
        model,database=self.make('BO_ 2566848779 '+('Long'*20)+': 8 ECU\n SG_ Value : 0|8@1+ (1,0) [0|255] "" ECU\n')
        self.assertIn('exceeds63',self.reject(model,database=database,symbol='custom_metadata'))
    def test_output_prefix_rejects_unsafe_suffix(self):
        p,out=self.cli(prefix=self.root/'bad.name');self.assertEqual(p.returncode,1)
        self.assertIn('output prefix',p.stderr);self.assertFalse(out.with_suffix('.c').exists())
    def test_insertion_can_renumber(self):
        p,a=self.cli();self.assertEqual(p.returncode,0,p.stderr);first=json.loads(a.with_suffix('.json').read_text())
        original=(ROOT/'tests/dbc/subscriptions_synthetic.dbc').read_text()
        model,database=self.make(original+'\nBO_ 99 Before: 8 ECU\n SG_ Added : 0|8@1+ (1,0) [0|255] "" ECU\n')
        p,a=self.cli(model,database=database,symbol='custom_metadata');self.assertEqual(p.returncode,0,p.stderr)
        second=json.loads(a.with_suffix('.json').read_text());self.assertNotEqual(first['catalog_sha256'],second['catalog_sha256'])
        first_id=next(s['id'] for s in first['signals'] if s['message']=='Counter');second_id=next(s['id'] for s in second['signals'] if s['message']=='Counter')
        self.assertEqual(second_id,first_id+1)
if __name__=='__main__':unittest.main()
