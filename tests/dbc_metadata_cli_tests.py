"""Strict metadata CLI regressions; synthetic inputs only, no capture needed."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

COMPILER = str(pathlib.Path(sys.argv.pop(1)).resolve())
FIXTURE = pathlib.Path(__file__).parent / 'dbc' / 'metadata_synthetic.dbc'

class MetadataCliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.c, self.h = self.root/'sample.c', self.root/'sample.h'
        self.c.write_text('C sentinel')
        self.h.write_text('H sentinel')

    def run_cli(self, *args):
        return subprocess.run([COMPILER, *map(str, args)], capture_output=True, text=True)

    def unchanged(self):
        self.assertEqual(self.c.read_text(), 'C sentinel')
        self.assertEqual(self.h.read_text(), 'H sentinel')

    def test_metadata_profile_and_legacy_omission(self):
        r = self.run_cli('-in', FIXTURE, '-c_path', self.c, '-name', 'sample', '-c-metadata')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('const j1939_db_metadata_t sample_metadata', self.c.read_text())
        self.assertIn('J1939_DB_METADATA_VERSION', self.h.read_text())
        r = self.run_cli('-in', FIXTURE, '-c_path', self.c, '-no-strings')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn('j1939_db_metadata', self.c.read_text())
        self.assertNotIn('j1939_signal_metadata', self.c.read_text())

    def test_incompatible_options_preserve_outputs(self):
        for flags in [('-no-strings',), ('-model_path', self.root/'model.txt')]:
            r = self.run_cli('-in', FIXTURE, '-c_path', self.c, '-c-metadata', *flags)
            self.assertEqual(r.returncode, 2, r.stderr)
            self.unchanged()
        r = self.run_cli('-in', FIXTURE, '-c-metadata')
        self.assertEqual(r.returncode, 2)

    def test_unsupported_declarations_reject_before_output(self):
        source = self.root/'float.dbc'
        source.write_text('BO_ 100 F: 8 ECU\n SG_ V : 0|32@1+ (1,0) [0|1] "" ECU\nSIG_VALTYPE_ 100 V : 1;\n')
        r = self.run_cli('-in', source, '-c_path', self.c, '-c-metadata')
        self.assertEqual(r.returncode, 4, r.stderr)
        self.assertIn('model-v1', r.stderr)
        self.unchanged()

    def test_missing_units_reject_but_explicit_empty_is_supported(self):
        source = self.root/'missing.dbc'
        source.write_text('BO_ 100 F: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] ECU\n')
        r = self.run_cli('-in', source, '-c_path', self.c, '-c-metadata')
        self.assertEqual(r.returncode, 4, r.stderr)
        self.unchanged()

    def test_differing_unit_aliases_are_not_silently_collapsed(self):
        source = self.root/'conflict.dbc'
        source.write_text('BO_ 2566849547 A: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] "unit A" ECU\n'
                          'BO_ 2566849598 B: 8 ECU\n SG_ V : 0|8@1+ (1,0) [0|255] "unit B" ECU\n')
        r = self.run_cli('-in', source, '-c_path', self.c, '-c-metadata')
        self.assertEqual(r.returncode, 4, r.stderr)
        self.assertIn('generate separate databases', r.stderr)
        self.unchanged()

if __name__ == '__main__':
    unittest.main()
