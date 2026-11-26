#!/usr/bin/env python3
"""CLI and independent protobuf Text Format consumer tests.
Run with generated bindings on PYTHONPATH:
  python3 tests/dbc_model_consumer_tests.py /path/to/dbc_compiler
"""
from decimal import Decimal
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from google.protobuf import text_format
import dbc_model_v1_pb2 as pb

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
COMPILER = Path(sys.argv.pop(1)).resolve()
FIXTURES = ROOT / "tests/dbc/model_v1"
spec = importlib.util.spec_from_file_location("consumer", ROOT / "examples/read_dbc_model.py")
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)


class ModelConsumerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.folder = Path(self.directory.name)
        self.output = self.folder / "model.textproto"

    def compile(self, sources, output=None, extra=()):
        command = [str(COMPILER)]
        for source in sources:
            command += ["-in", str(source)]
        command += ["-model_path", str(output or self.output), *extra]
        return subprocess.run(command, text=True, capture_output=True)

    def test_legacy_declarations_are_ignored_only_by_legacy_outputs(self):
        for declaration in ('SIG_VALTYPE_ 100 Value : 0;', 'SIG_VALTYPE_ 100 Value : 1;',
                            'SG_MUL_VAL_ 100 Value Switch 0-1;'):
            source=self.folder/'legacy.dbc'
            source.write_text('BO_ 100 Sample: 8 ECU\n SG_ Value : 0|8@1+ (1,0) [0|255] "" ECU\n\n'+declaration+'\n')
            for mode in ('-pgn_path','-union_path'):
                output=self.folder/'legacy-output.h'
                result=subprocess.run([str(COMPILER),'-in',str(source),mode,str(output)],text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertTrue(output.read_text())
            for mode in ('-c_path','-accessor_path','-model_path'):
                output=self.folder/'portable-output.c'
                output.write_text('sentinel')
                result=subprocess.run([str(COMPILER),'-in',str(source),mode,str(output)],text=True,capture_output=True)
                self.assertEqual(result.returncode,4,result.stderr)
                self.assertEqual(output.read_text(),'sentinel')

    def test_host_script_link_flags_default_for_blank_values(self):
        # Exercise real Bash argument expansion without rebuilding the host suite.
        # Native compilation/execution is covered by the separate focused gate.
        bash='/bin/bash' if Path('/bin/bash').exists() else shutil.which('bash')
        if not bash:self.skipTest('bash unavailable')
        tools=self.folder/'tools';tools.mkdir()
        (tools/'cc').write_text('#!/bin/sh\nexit 0\n')
        (tools/'c++').write_text('#!'+sys.executable+'\n'+
            'import json,os,pathlib,sys\n'+
            'pathlib.Path(os.environ["ARG_LOG"]).write_text(json.dumps(sys.argv[1:]))\n'+
            'p=pathlib.Path(sys.argv[sys.argv.index("-o")+1])\n'+
            'p.write_text("#!/bin/sh\\nexit 0\\n");p.chmod(0o755)\n')
        for tool in tools.iterdir():tool.chmod(0o755)
        log=self.folder/'args.json'
        for flags in (None,'',' ','\t\n','-Lcustom -lcustom'):
            env=dict(os.environ,PATH=str(tools)+os.pathsep+os.environ['PATH'],
                     J1939_HOST_BUILD_DIR=str(self.folder/'host'),J1939_HOST_CXXFLAGS='',ARG_LOG=str(log))
            env.pop('J1939_GTEST_FLAGS',None)
            if flags is not None:env['J1939_GTEST_FLAGS']=flags
            r=subprocess.run([bash,str(ROOT/'scripts/quarry_host_tests.sh')],env=env,text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stderr)
            import json
            args=json.loads(log.read_text())
            expected=['-Lcustom','-lcustom'] if flags and flags.strip() else ['-lgtest','-lgtest_main','-pthread']
            for flag in expected:self.assertIn(flag,args)
            if expected[0]=='-Lcustom':self.assertNotIn('-lgtest',args)

    def test_mixed_outputs_preserve_all_files_on_portable_validation_failure(self):
        source=self.folder/'mixed.dbc'
        declarations=('SIG_VALTYPE_ 100 A : 1;', 'SG_MUL_VAL_ 100 A Switch 0-2;')
        base='BO_ 100 Sample: 8 ECU\n SG_ A : 0|8@1+ (1,0) [0|1] "" ECU\n\n'
        outputs=[self.folder/n for n in ('legacy.pgn','legacy.union','portable.c','portable.h','accessors.h')]
        for declaration in declarations:
            source.write_text(base+declaration+'\n')
            for legacy,index in (('-pgn_path',0),('-union_path',1)):
                for portable,target in (('-c_path',outputs[2]),('-accessor_path',outputs[4])):
                    for p in outputs:p.write_text('sentinel')
                    r=subprocess.run([str(COMPILER),'-in',str(source),legacy,str(outputs[index]),portable,str(target)],text=True,capture_output=True)
                    self.assertEqual(r.returncode,4,r.stderr)
                    for p in outputs:self.assertEqual(p.read_text(),'sentinel',str(p))
        # C output itself is valid, but accessor identifiers collide. Nothing may
        # be opened until the later accessor validation has also succeeded.
        source.write_text('BO_ 100 Sample: 8 ECU\n SG_ A-B : 0|8@1+ (1,0) [0|1] "" ECU\n SG_ A_B : 8|8@1+ (1,0) [0|1] "" ECU\n')
        args=[str(COMPILER),'-in',str(source),'-pgn_path',str(outputs[0]),'-union_path',str(outputs[1]),'-c_path',str(outputs[2]),'-accessor_path',str(outputs[4])]
        for p in outputs:p.write_text('sentinel')
        r=subprocess.run(args,text=True,capture_output=True)
        self.assertEqual(r.returncode,4,r.stderr)
        for p in outputs:self.assertEqual(p.read_text(),'sentinel',str(p))
        source.write_text(base)
        r=subprocess.run(args,text=True,capture_output=True)
        self.assertEqual(r.returncode,0,r.stderr)
        for p in outputs:self.assertNotEqual(p.read_text(),'sentinel',str(p))

    def test_malformed_decimal_metadata_rejected_by_c_database_and_model(self):
        source=self.folder/'decimal.dbc'
        for values in [('1.2.3','0','0','1'),('1','08foo','0','1'),
                       ('1','0','0junk','1'),('1','0','0','1.2.3')]:
            scale,offset,minimum,maximum=values
            source.write_text(f'BO_ 100 Sample: 8 ECU\n SG_ A : 0|8@1+ ({scale},{offset}) [{minimum}|{maximum}] "" ECU\n')
            for mode in ('-c_path','-model_path'):
                self.output.write_text('sentinel')
                r=subprocess.run([str(COMPILER),'-in',str(source),mode,str(self.output)],text=True,capture_output=True)
                self.assertEqual(r.returncode,4,r.stderr)
                self.assertEqual(self.output.read_text(),'sentinel')

    def test_consumer_values_provenance_and_selection(self):
        result = self.compile([FIXTURES / "single_frame.dbc", FIXTURES / "multiplex.dbc"])
        self.assertEqual(result.returncode, 0, result.stderr)
        model = consumer.load_model(self.output)
        self.assertEqual(model.version, 1)
        message = consumer.select_message(model, 100)
        self.assertEqual((message.name, message.source, message.line), ("Mixed", "single_frame.dbc", 10))
        self.assertFalse(message.extended)
        self.assertFalse(message.HasField("pgn"))
        self.assertEqual(message.spn, 61444)
        signals = {signal.name: signal for signal in message.signals}
        intel, motorola = signals["Intel"], signals["Motorola"]
        self.assertEqual((intel.start_bit, intel.width, intel.byte_order, intel.is_signed), (3, 13, pb.Signal.INTEL, False))
        self.assertEqual(intel.scale, "0.5000")
        self.assertEqual(intel.unit, "rev per min")
        self.assertTrue(intel.HasField("spn"))
        self.assertEqual(intel.spn, 0)  # explicit zero differs from absent
        self.assertEqual(intel.line, 11)
        self.assertEqual(Decimal(4660) * Decimal(intel.scale) + Decimal(intel.offset), Decimal(2328))
        self.assertEqual((motorola.start_bit, motorola.width, motorola.byte_order, motorola.is_signed), (23, 12, pb.Signal.MOTOROLA, True))
        self.assertFalse(motorola.HasField("spn"))
        self.assertEqual(Decimal(-1348) * Decimal(motorola.scale) + Decimal(motorola.offset), Decimal(-327))
        self.assertEqual(signals["Status"].unit, "")
        self.assertEqual([(e.raw, e.label) for e in signals["Status"].enums], [(0, "Off"), (255, "Not available")])
        extended = consumer.select_message(model, 2565866513)
        self.assertEqual((extended.can_id, extended.pgn, extended.source_address), (418382865, 61444, 17))
        self.assertTrue(extended.HasField("j1939_default"))
        self.assertFalse(extended.j1939_default)
        self.assertFalse(message.HasField("j1939_default"))
        mux = consumer.select_message(model, 200)
        branch = next(s for s in mux.signals if s.name == "Signed").multiplex
        self.assertEqual((branch.role, branch.switch_name, branch.value), (pb.Multiplex.BRANCH, "Switch", 0))
        self.assertTrue(branch.HasField("value"))
        for source in model.sources:
            data = (FIXTURES / source.name).read_bytes()
            checksum = 14695981039346656037
            for byte in data:
                checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
            self.assertEqual(source.fnv1a64, f"{checksum:016x}")
            self.assertEqual(source.byte_length, len(data))
        self.assertFalse(next(s for s in model.sources if s.name == "multiplex.dbc").HasField("dbc_version"))
        selected = subprocess.run([sys.executable, str(ROOT / "examples/read_dbc_model.py"), str(self.output), "--dbc-id", "100"], capture_output=True, text=True)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        self.assertEqual(text_format.Parse(selected.stdout, pb.Message()), message)

    def test_determinism_input_order_relocation_and_golden(self):
        sources = [FIXTURES / "single_frame.dbc", FIXTURES / "multiplex.dbc"]
        self.assertEqual(self.compile(sources).returncode, 0)
        first = self.output.read_bytes()
        self.assertEqual(first, (FIXTURES / "expected.textproto").read_bytes())
        copied = self.folder / "relocated"
        shutil.copytree(FIXTURES, copied)
        result = self.compile([copied / source.name for source in reversed(sources)])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), first)
        self.assertNotIn(str(self.folder).encode(), first)

    def test_rejection_preserves_existing_output_and_reports_line(self):
        self.output.write_text("SENTINEL")
        result = self.compile([FIXTURES / "unsupported_float.dbc"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported_float.dbc:4: model-v1:", result.stderr)
        self.assertIn("SIG_VALTYPE_", result.stderr)
        self.assertEqual(self.output.read_text(), "SENTINEL")
        result = self.compile([FIXTURES / "single_frame.dbc"], extra=["-accessor_path", str(self.folder / "accessors.h")])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.folder / "accessors.h").exists())
        self.assertEqual(self.output.read_text(), "SENTINEL")

    def test_duplicate_source_names_and_unknown_versions_or_fields(self):
        source = FIXTURES / "single_frame.dbc"
        result = self.compile([source, source])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate source logical name", result.stderr)
        self.assertFalse(self.output.exists())
        for text in ('format: "j1939.dbc-model" version: 2', 'format: "other" version: 1',
                     'format: "j1939.dbc-model"', 'format: "j1939.dbc-model" version: 1 unknown: 0'):
            self.output.write_text(text)
            with self.assertRaises((ValueError, text_format.ParseError)):
                consumer.load_model(self.output)

    def test_explicit_empty_headers_and_text_escaping(self):
        source = self.folder / "strings.dbc"
        source.write_text('VERSION ""\nNS_ :\n\nBU_:\nBO_ 1 Text: 1 ECU\n SG_ Value : 0|8@1+ (1,0) [0|255] "m\tper s" Receiver\n')
        result = self.compile([source])
        self.assertEqual(result.returncode, 0, result.stderr)
        model = consumer.load_model(self.output)
        self.assertTrue(model.sources[0].HasField("dbc_version"))
        self.assertEqual(model.sources[0].dbc_version, "")
        self.assertTrue(model.sources[0].HasField("nodes"))
        self.assertTrue(model.sources[0].HasField("namespace_symbols"))
        self.assertEqual(list(model.sources[0].nodes.values), [])
        self.assertEqual(model.messages[0].signals[0].unit, "m\tper s")
        self.assertIn(r'unit: "m\011per s"', self.output.read_text())

    def test_crlf_and_wide_enum_precision(self):
        source = self.folder / "wide.dbc"
        source.write_bytes(b'BO_ 1 Wide: 8 ECU\r\n SG_ Value : 0|64@1+ (1,0) [0|18446744073709551615] "" Receiver\r\nVAL_ 1 Value 18446744073709551615 "Maximum" ;\r\n')
        result = self.compile([source])
        self.assertEqual(result.returncode, 0, result.stderr)
        model = consumer.load_model(self.output)
        signal = model.messages[0].signals[0]
        self.assertEqual(signal.enums[0].raw, 18446744073709551615)
        self.assertEqual(signal.maximum, "18446744073709551615")
        self.assertFalse(model.sources[0].HasField("nodes"))
        self.assertFalse(model.sources[0].HasField("namespace_symbols"))


if __name__ == "__main__":
    unittest.main()
