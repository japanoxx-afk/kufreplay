import hashlib
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from engine import (MAGIC, STATE, INITIAL_SIZE, parse_trace, resolve_map, package_capture,
                    stage_playback, collect_completed, recover_closed_capture, ADAPTER, GAME_HASH, replay_time, BOOTSTRAP)
from replay_store import Store, validate

def record(kind, payload):
    return struct.pack('<2I',kind,len(payload))+payload

def fixture(directory=b'res\\map\\Skirmish\\', name=b'fixture.kfm', network_mode=0):
    initial=bytearray(INITIAL_SIZE)
    before=(1,0,0,100)
    STATE.pack_into(initial,0,*before)
    struct.pack_into('<I',initial,16,network_mode)
    initial[20:20+len(directory)]=directory
    initial[276:276+len(name)]=name
    return (MAGIC+record(1,initial)+record(2,STATE.pack(*before))+
            record(3,struct.pack('<4I',1,0,0,4)+b'\x05\x00\x00\x00')+
            record(4,STATE.pack(1,1,1,107))+record(5,STATE.pack(2,1,1,110)))

def network_fixture():
    values=[1,0,1,3,0,123,45,54]
    for i in range(11): values.extend([1 if i==0 else 2,1000,1,1])
    boot=BOOTSTRAP.pack(*values,b'\x01\x01\x00\x01'+bytes(12))
    raw=fixture(network_mode=1);start=8+8+INITIAL_SIZE
    return raw[:start]+record(8,boot)+raw[start:]

class EngineTests(unittest.TestCase):
    def test_network_bootstrap_and_validation(self):
        raw=network_fixture();trace=parse_trace(raw)
        self.assertEqual(len(trace.bootstrap),224)
        self.assertEqual(BOOTSTRAP.unpack(trace.bootstrap)[7],54)
        start=8+8+INITIAL_SIZE
        for field,value in [(0,2),(3,12),(4,8),(7,0),(8,4),(9,999),(10,65536),(11,2)]:
            bad=bytearray(raw);struct.pack_into('<I',bad,start+8+field*4,value)
            with self.assertRaises(ValueError,msg=f'bootstrap field {field}'):
                parse_trace(bad)
        with self.assertRaises(ValueError):
            parse_trace(raw[:start]+record(8,trace.bootstrap)+raw[start:])
        single=bytearray(raw);struct.pack_into('<I',single,8+8+16,0)
        with self.assertRaises(ValueError): parse_trace(single)

    def test_network_options_padding_is_not_an_enum(self):
        raw=bytearray(network_fixture()); start=8+8+INITIAL_SIZE+8
        raw[start+217:start+220]=b'\xcd\xcd\xcd'
        self.assertEqual(BOOTSTRAP.unpack(parse_trace(raw).bootstrap)[-1][9:12],b'\xcd\xcd\xcd')
        raw[start+216]=205
        with self.assertRaisesRegex(ValueError,'초기화 설정'): parse_trace(raw)

    def test_network_auxiliary_slot_preserves_native_word(self):
        raw=bytearray(network_fixture()); start=8+8+INITIAL_SIZE+8
        struct.pack_into('<I',raw,start+(8+10*4)*4,1952796276)
        self.assertEqual(BOOTSTRAP.unpack(parse_trace(raw).bootstrap)[48],1952796276)
        struct.pack_into('<I',raw,start+(8+9*4)*4,1952796276)
        with self.assertRaisesRegex(ValueError,'참가자 설정'): parse_trace(raw)

    def test_deferred_network_bootstrap_after_start_command(self):
        raw=fixture(network_mode=1);start=8+8+INITIAL_SIZE+8+STATE.size
        boot=bytearray(parse_trace(network_fixture()).bootstrap)
        struct.pack_into('<I',boot,8*4,3) # Observed real network controller enum.
        packet=struct.pack('<H',1007)+bytes(94)
        command=record(3,struct.pack('<4I',1,0,0,96)+packet)
        late=raw[:start]+command+record(8,boot)+raw[start+8+20:]
        trace=parse_trace(late)
        self.assertEqual(trace.bootstrap,boot)
        self.assertEqual(trace.commands,1)
        with self.assertRaisesRegex(ValueError,'초기화 명령'):
            parse_trace(raw[:start]+record(8,boot)+raw[start:])

    def test_network_bootstrap_authenticated_staging(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            source=root/'capture.partial.complete';source.write_bytes(network_fixture())
            store=Store(root/'library')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                replay=package_capture(source,game,store);metadata=validate(replay)
                self.assertTrue(metadata['multiplayer_supported'])
                self.assertTrue(metadata['playback_supported'])
                self.assertEqual(metadata['initial_restore'],'automatic-network-bootstrap')
                self.assertEqual(metadata['tick_interval_ms'],54)
                staged,trace=stage_playback(replay,game,store)
                self.assertEqual(staged.read_bytes(),network_fixture())
                self.assertIsNotNone(trace.bootstrap)
    def test_recollect_auxiliary_slot_rejection_once(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            store=Store(root/'library');inbox=store.root/'CaptureInbox';inbox.mkdir()
            source=inbox/'123_1.partial.complete.rejected'
            raw=bytearray(network_fixture())
            struct.pack_into('<I',raw,8+8+INITIAL_SIZE+8+48*4,1952796276)
            source.write_bytes(raw)
            source.with_suffix(source.suffix+'.json').write_text(json.dumps({'error':'손상된 멀티 참가자 설정'}),encoding='utf-8')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                done,errors=collect_completed(game,store)
                self.assertEqual((len(done),errors),(1,[]))
                with zipfile.ZipFile(done[0]) as z: self.assertEqual(z.read('commands.bin'),raw)
                self.assertEqual(collect_completed(game,store),([],[]))

    def test_network_capture_parser(self):
        trace=parse_trace(fixture(network_mode=1))
        self.assertEqual(trace.network_mode,1)
        self.assertEqual(trace.commands,1)
        self.assertEqual(trace.frames[0][1][0],(1,0,0,b'\x05\x00\x00\x00'))
        with self.assertRaisesRegex(ValueError,'네트워크'):
            parse_trace(fixture(network_mode=2))

    def test_network_capture_package_blocks_unimplemented_playback(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            source=root/'capture.partial.complete';source.write_bytes(fixture(network_mode=1))
            store=Store(root/'library')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                replay=package_capture(source,game,store)
                metadata=validate(replay)
                self.assertEqual(metadata['recording_mode'],'multiplayer')
                self.assertEqual(metadata['network_mode'],1)
                self.assertFalse(metadata['playback_supported'])
                self.assertEqual(metadata['initial_restore'],'multiplayer-lobby-restore-required')
                self.assertEqual(len(store.listing()),1)
                self.assertFalse(source.exists())
                with self.assertRaisesRegex(ValueError,'초기화 설정'):
                    stage_playback(replay,game,store)
                self.assertFalse((store.root/'Playback').exists())

    def test_replay_time_including_legacy_metadata(self):
        metadata={'adapter_id':ADAPTER,'game_sha256':GAME_HASH,'ticks':4621,
                  'tick_rate':25,'duration_verified':False}
        self.assertEqual(replay_time(metadata),'2:18')
        metadata['ticks']=1849
        self.assertEqual(replay_time(metadata),'0:55')
        metadata['ticks']=120000
        self.assertEqual(replay_time(metadata),'60:00')
        self.assertEqual(replay_time({'ticks':120,'tick_rate':30}),'0:04')

    def test_automatic_setup_versions_and_bounds(self):
        raw=fixture();start=8+8+INITIAL_SIZE
        old=struct.pack('<21I',1,0,1,0,0,*([0,0]+[1]*6),*([0,1]+[0]*6))
        modern=struct.pack('<I',2)+old[4:]+bytes([1,1,0,1,1,0,0,0,1,0,0,0,0,0,0,0])
        for setup in (old,modern):
            self.assertEqual(parse_trace(raw[:start]+record(7,setup)+raw[start:]).setup,setup)
        invalid=bytearray(modern);invalid[87]=255
        for setup in (modern[:-1],bytes(invalid),struct.pack('<I',9)+old[4:]):
            with self.assertRaises(ValueError):
                parse_trace(raw[:start]+record(7,setup)+raw[start:])
        with self.assertRaises(ValueError):
            parse_trace(raw[:start]+record(7,old)+record(7,old)+raw[start:])

    def test_native_wire_layout(self):
        t=parse_trace(fixture())
        self.assertEqual(t.map_name,'fixture.kfm')
        self.assertEqual(t.map_directory,'res\\map\\Skirmish\\')
        self.assertEqual(t.frames[0][1][0],(1,0,0,b'\x05\x00\x00\x00'))
        self.assertEqual(t.final,(2,1,1,110))
        self.assertEqual(t.commands,1)

    def test_all_truncations_rejected(self):
        raw=fixture()
        for length in range(len(raw)):
            with self.assertRaises(ValueError,msg=f'cut:{length}'):
                parse_trace(raw[:length])

    def test_commands_need_a_tick(self):
        initial=fixture()[8:8+8+INITIAL_SIZE]
        bad=MAGIC+initial+record(3,struct.pack('<4I',1,0,0,2)+b'xx')
        with self.assertRaises(ValueError):
            parse_trace(bad)

    def test_tick_order_and_command_size(self):
        raw=fixture()
        for offset,value in [(8+8+INITIAL_SIZE+8,0), (8+8+INITIAL_SIZE+8+16+8+12,2049)]:
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):
                parse_trace(bytes(bad))

    def test_end_must_be_last(self):
        with self.assertRaises(ValueError):
            parse_trace(fixture()+record(5,STATE.pack(2,1,1,110)))

    def test_map_path_escape_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            game=Path(root)/'install'/'KingdomUnderFire.exe';game.parent.mkdir()
            outside=Path(root)/'fixture.kfm';outside.write_bytes(b'map')
            for directory in (b'../',b'C:/outside/',b'/outside/'):
                with self.assertRaises(ValueError):
                    resolve_map(game,parse_trace(fixture(directory=directory)))

    def test_package_and_authenticated_stage(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            absolute_trace=parse_trace(fixture(directory=(str(maps)+'\\').encode('cp949')))
            self.assertEqual(resolve_map(game,absolute_trace),maps/'fixture.kfm')
            source=root/'capture.partial.complete';source.write_bytes(fixture())
            store=Store(root/'library')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                replay=package_capture(source,game,store)
                self.assertFalse(source.exists())
                metadata=validate(replay)
                self.assertEqual(metadata['adapter_id'],ADAPTER)
                self.assertTrue(metadata['duration_verified'])
                self.assertEqual(metadata['tick_interval_ms'],30)
                staged,trace=stage_playback(replay,game,store)
                self.assertEqual(staged.read_bytes(),fixture())
                self.assertEqual(trace.commands,1)
                # A different installation has no local authenticity key.
                foreign=Store(root/'foreign')
                with self.assertRaises(ValueError):
                    stage_playback(replay,game,foreign)
                # Metadata tampering remains prohibited even with identical byte hashes.
                with zipfile.ZipFile(replay) as z:
                    contents={name:z.read(name) for name in z.namelist()}
                altered=json.loads(contents['manifest.json']);altered['map_id']='changed'
                contents['manifest.json']=json.dumps(altered).encode()
                bad=root/'tampered.kufreplay'
                with zipfile.ZipFile(bad,'w') as z:
                    for name,data in contents.items(): z.writestr(name,data)
                with self.assertRaises(ValueError):
                    stage_playback(bad,game,store)
                # Replacement between validation and reopening must not inject different bytes.
                changed=dict(contents)
                changed['manifest.json']=json.dumps(metadata).encode()
                changed['commands.bin']=fixture()[:-1]+bytes([fixture()[-1]^1])
                swapped=root/'swapped.kufreplay'
                with zipfile.ZipFile(swapped,'w') as z:
                    for name,data in changed.items(): z.writestr(name,data)
                with patch('engine.validate',return_value=metadata):
                    with self.assertRaises(ValueError):
                        stage_playback(swapped,game,store)

    def test_failed_completed_capture_quarantined_once(self):
        with tempfile.TemporaryDirectory() as root:
            store=Store(root);inbox=store.root/'CaptureInbox';inbox.mkdir()
            source=inbox/'bad.partial.complete';source.write_bytes(b'broken')
            done,errors=collect_completed('unused',store)
            self.assertEqual(done,[]);self.assertEqual(len(errors),1)
            self.assertFalse(source.exists())
            self.assertTrue(list(inbox.glob('*.rejected')))
            self.assertEqual(collect_completed('unused',store),([],[]))

    def test_recover_closed_prefix_preserves_source_and_cutoff_state(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            source=root/'ended.partial'
            second=(2,1,1,110)
            raw=fixture()[:-24]+record(2,STATE.pack(*second))+record(4,STATE.pack(*second))
            source.write_bytes(raw)
            store=Store(root/'store')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                replay=recover_closed_capture(source,game,store)
            self.assertEqual(source.read_bytes(),raw)
            metadata=validate(replay)
            self.assertEqual(metadata['recovery']['discarded_batches'],1)
            with zipfile.ZipFile(replay) as z: trace=parse_trace(z.read('commands.bin'))
            self.assertEqual(len(trace.frames),1)
            self.assertEqual(trace.final,second)
            self.assertEqual(trace.commands,1)
            source.write_bytes(raw[:-1])
            with self.assertRaises(ValueError): recover_closed_capture(source,game,store)

    def test_orphan_recovery_waits_for_process_exit_and_handles_torn_tail(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);game=root/'game'/'KingdomUnderFire.exe'
            game.parent.mkdir();game.write_bytes(b'fixture-executable')
            maps=game.parent/'res'/'map'/'Skirmish';maps.mkdir(parents=True)
            (maps/'fixture.kfm').write_bytes(b'fixture-map-data')
            store=Store(root/'store');inbox=store.root/'CaptureInbox';inbox.mkdir()
            source=inbox/'54321_1.partial';second=(2,1,1,110)
            source.write_bytes(fixture()[:-24]+record(2,STATE.pack(*second))+record(4,STATE.pack(*second))+struct.pack('<2I',2,16)+b'xx')
            with patch('engine.GAME_HASH',hashlib.sha256(game.read_bytes()).hexdigest()):
                with patch('engine.process_alive',return_value=True):
                    self.assertEqual(collect_completed(game,store),([],[]))
                    self.assertTrue(source.exists())
                with patch('engine.process_alive',return_value=False):
                    completed,errors=collect_completed(game,store)
                    self.assertEqual(errors,[]);self.assertEqual(len(completed),1)
                    self.assertEqual(validate(completed[0])['recovery']['truncated_tail_bytes'],10)
                    self.assertTrue(source.with_suffix('.partial.recovered').exists())
                    self.assertEqual(collect_completed(game,store),([],[]))

if __name__=='__main__':
    unittest.main()
