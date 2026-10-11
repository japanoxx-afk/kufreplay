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
                    stage_playback, collect_completed, ADAPTER)
from replay_store import Store, validate

def record(kind, payload):
    return struct.pack('<2I',kind,len(payload))+payload

def fixture(directory=b'res\\map\\Skirmish\\', name=b'fixture.kfm'):
    initial=bytearray(INITIAL_SIZE)
    before=(1,0,0,100)
    STATE.pack_into(initial,0,*before)
    initial[20:20+len(directory)]=directory
    initial[276:276+len(name)]=name
    return (MAGIC+record(1,initial)+record(2,STATE.pack(*before))+
            record(3,struct.pack('<4I',1,0,0,4)+b'\x05\x00\x00\x00')+
            record(4,STATE.pack(1,1,1,107))+record(5,STATE.pack(2,1,1,110)))

class EngineTests(unittest.TestCase):
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
                self.assertFalse(metadata['duration_verified'])
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

if __name__=='__main__':
    unittest.main()
