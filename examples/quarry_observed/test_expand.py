"""Redistributable, capture-independent selection and oracle regression tests."""
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from expand import active, bits, raw_value, select, unpack, validate_definition, read_capture, enforce_coverage, expect_rejection, wrong_pgn_id, pin_coverage, verify_bundle, save, load_private_profile


def signal(name='V', start=0, width=8, order=0, signed=False, **kw):
    return dict(name=name, start_bit=start, width=width, byte_order=order,
                is_signed=signed, scale='1', offset='0', minimum='0', maximum='255',
                unit='', spn=None, enums=[], is_mux_switch=False,
                multiplexed=False, mux_value=0, **kw)


def message(sa=11, unit='A'):
    s = signal()
    s['unit'] = unit
    return dict(id=0x98ff0400+sa, pgn=0xff04, sa=sa, name='Sample', dlc=8, signals=[s])


def frame(sa=11):
    return dict(extended=True, pgn=0xff04, sa=sa, da=255, dlc=8)


class ObservedMappingTests(unittest.TestCase):
    def test_coverage_report_is_verified(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            b=Path(directory)
            save(b/'manifest.json',dict(format='j1939.observed.mapping',version=3,artifacts={}))
            pin_coverage(b,dict(raw=32))
            self.assertIn('coverage.json',verify_bundle(b)['artifacts'])
            save(b/'coverage.json',dict(raw=999))
            with self.assertRaisesRegex(ValueError,'coverage.json'): verify_bundle(b)

    def test_private_profile_requires_explicit_reviewed_fingerprints(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'profile.json'
            valid=dict(reviewed_dbc_sha256=['a'*64],reviewed_capture_sha256='b'*64)
            save(p,valid)
            self.assertEqual(load_private_profile(p),({'a'*64},'b'*64))
            for invalid in [{},dict(valid,extra='not allowed'),dict(valid,reviewed_dbc_sha256=['invalid'])]:
                save(p,invalid)
                with self.assertRaises(ValueError):load_private_profile(p)

    def test_coverage_exit_policy(self):
        from collections import Counter
        expected=Counter({'conflicting definitions: no unique exact source-address match; configure routing/default':1,
                          'no DBC definition':1,'transport fragment; reassemble before mapping':2,
                          'short/incorrect DLC 4; expected 8':1,'CAN FD/oversized frame unsupported':1})
        enforce_coverage({},False)
        enforce_coverage(expected,True)
        for skipped,synthetic in [(expected,False),({},True),(expected+Counter({'new':1}),True)]:
            with self.assertRaisesRegex(ValueError,'coverage'):
                enforce_coverage(skipped,synthetic)

    def test_rejection_requires_exact_status_and_clean_stderr(self):
        import subprocess
        for status,stderr in [(-6,''),(1,''),(3,'AddressSanitizer failure')]:
            with patch('expand.subprocess.run',return_value=subprocess.CompletedProcess([],status,'',stderr)):
                with self.assertRaisesRegex(ValueError,'clean rejection'):
                    expect_rejection('unused','input',3)
        with patch('expand.subprocess.run',return_value=subprocess.CompletedProcess([],3,'','')):
            expect_rejection('unused','input',3)

    def test_wrong_pgn_including_zero(self):
        for pgn in (0,0x100,0x200,0xf004):
            self.assertNotEqual(unpack(wrong_pgn_id(pgn))[0],pgn)

    def test_request_metadata_conflicts_reject(self):
        a=message(42);a.update(pgn=0xea00,dlc=3,request_default=True)
        a['signals']=[signal(width=24)]
        f=frame(249);f.update(pgn=0xea00,dlc=3)
        for field,value in [('unit','different'),('minimum','-1'),('maximum','256'),
                            ('enums',[{'raw':1,'label':'one'}]),('multiplexed',True),
                            ('is_mux_switch',True),('spn',0)]:
            b=copy.deepcopy(a);b['signals'][0][field]=value
            with self.subTest(field=field), self.assertRaises(ValueError): select([a,b],f)

    def test_unreviewed_capture_rejects_before_legacy_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'unreviewed.ros'
            path.write_text('messages: []\n')
            with patch('expand.run',side_effect=AssertionError('reader must not run')):
                with self.assertRaisesRegex(ValueError,'reviewed capture'):
                    read_capture(Path(directory),path,Path(directory),[],[])

    def test_independent_cross_byte_oracles(self):
        p = bytes.fromhex('a791abcf00000000')
        self.assertEqual(bits(p, signal(start=3,width=13)), 0x1234)
        self.assertEqual(raw_value(p, signal(start=3,width=13,signed=True)), -3532)
        self.assertEqual(bits(p, signal(start=23,width=12,order=1)), 0xabc)
        self.assertEqual(raw_value(p, signal(start=23,width=12,order=1,signed=True)), -1348)

    def test_full_width_signed_and_unsigned(self):
        p = bytes.fromhex('0000000000000080')
        self.assertEqual(raw_value(p, signal(width=64,signed=True)), -(1<<63))
        self.assertEqual(raw_value(b'\xff'*8, signal(width=64)), (1<<64)-1)
        self.assertEqual(raw_value(b'\xff'*8, signal(width=64,signed=True)), -1)

    def test_destination_does_not_change_pdu1_pgn(self):
        self.assertEqual(unpack(0x18ea2a0b), (0xea00,11,42))
        self.assertEqual(unpack(0x18eaff0b), (0xea00,11,255))
        self.assertEqual(unpack(0x18ff040b), (0xff04,11,255))

    def test_conflicting_units_route_by_exact_source(self):
        a,b = message(11,'m/s²'),message(62,'m/sÂ²')
        selected, aliases, policy = select([a,b],frame())
        self.assertIs(selected,a)
        self.assertEqual(aliases,[a])
        self.assertIn('exact',policy)
        self.assertIs(select([a,b],frame(62))[0],b)
        with self.assertRaisesRegex(ValueError,'source-address'):
            select([a,b],frame(33))

    def test_equivalent_aliases_accept_other_sources(self):
        a,b = message(11),message(62)
        _, aliases, policy = select([a,b],frame(33))
        self.assertEqual(len(aliases),2)
        self.assertIn('generic',policy)

    def test_proprietary_supplement_requires_declared_source(self):
        m=message();m['declared_sources_only']=True
        self.assertIs(select([m],frame())[0],m)
        with self.assertRaisesRegex(ValueError,'declared source'):
            select([m],frame(33))

    def test_request_default_is_explicit_and_layout_checked(self):
        a,b=message(42),message(19)
        for m in (a,b):
            m.update(pgn=0xea00,dlc=3)
            m['signals']=[signal('RequestedPgn',width=24)]
        b['signals'][0]['name']='DifferentName'
        a['request_default']=True
        f=frame(249);f.update(pgn=0xea00,dlc=3,da=42)
        self.assertIs(select([a,b],f)[0],a)
        b['signals'][0]['width']=16
        with self.assertRaisesRegex(ValueError,'non-ISO-Request'):
            select([a,b],f)

    def test_same_source_conflict_is_never_first_wins(self):
        with self.assertRaisesRegex(ValueError,'conflicting'):
            select([message(11,'A'),message(11,'B')],frame())

    def test_transport_unknown_fd_and_short_are_rejected(self):
        for changes, pattern in [({'pgn':0xec00},'transport'), ({'pgn':0xeb00},'transport'),
                                 ({'fd':True},'FD'), ({'dlc':9},'FD'),
                                 ({'dlc':7},'DLC'), ({'extended':False},'standard')]:
            f=frame(); f.update(changes)
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError,pattern):
                select([message()],f)
        with self.assertRaisesRegex(ValueError,'no DBC'):
            select([],frame())

    def test_active_mux_and_no_matching_branch(self):
        sw=signal('Switch',32,4); sw['is_mux_switch']=True
        a=signal('A',40); a.update(multiplexed=True,mux_value=0)
        b=signal('B',40); b.update(multiplexed=True,mux_value=1)
        m=message(); m['signals']=[sw,a,b]
        validate_definition(m)
        for value,expected in [(0,(True,False)),(1,(False,True)),(2,(False,False))]:
            p=bytes([0,0,0,0,value,42,0,0])
            self.assertEqual((active(p,a,m['signals']),active(p,b,m['signals'])),expected)

    def test_no_partial_schema_on_bad_signals(self):
        cases=[]
        for attr,val in [('width',0),('width',65),('start_bit',64),('scale','NaN'),('offset','inf'),('scale','0'),('name','not-valid')]:
            m=message();m['signals'][0][attr]=val;cases.append(m)
        m=message();m['signals'].append(copy.deepcopy(m['signals'][0]));cases.append(m)
        m=message();m['signals'][0]['multiplexed']=True;cases.append(m)
        m=message();m['signals'][0].update(is_mux_switch=True,is_signed=True);cases.append(m)
        m=message();m['signals']=[];cases.append(m)
        for m in cases:
            with self.subTest(m=m), self.assertRaises(ValueError):
                validate_definition(m)


if __name__ == '__main__':
    unittest.main()
