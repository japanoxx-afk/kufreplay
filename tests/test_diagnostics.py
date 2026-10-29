import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from diagnostics import archive_engine_log,create_report,create_report_zip,log_error
from diagnostics import game_identity
from engine import supported_game_hash, GAME_HASH, UPDATED_GAME_HASH
from engine import launch_engine
from replay_store import Store
from unittest.mock import patch
import hashlib
import json
import zipfile

class DiagnosticsTests(unittest.TestCase):
    def test_zip_keeps_selected_recovery_raw_tail_and_population_without_keys(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);inbox=root/'CaptureInbox';inbox.mkdir();replays=root/'Replays';replays.mkdir()
            (root/'engine.log').write_text('MS01 evidence')
            (root/'engine-local.key').write_bytes(b'secret')
            raw=b'KUFTRC01'+b'original incomplete tail'
            (inbox/'29868_1.partial.recovered').write_bytes(raw)
            (inbox/'29868_1.partial.population').write_bytes(b'KUFPOP01')
            (inbox/'not-a-capture.exe').write_bytes(b'executable')
            selected=replays/'selected.kufreplay'
            with zipfile.ZipFile(selected,'w') as z:
                z.writestr('manifest.json',json.dumps({'recovery':{'source':'29868_1.partial'}}))
            archive=create_report_zip(root,replay=selected)
            self.assertTrue(archive.is_file())
            self.assertFalse(Path(str(archive)+'.partial').exists())
            with zipfile.ZipFile(archive) as z:
                self.assertIsNone(z.testzip())
                self.assertEqual(z.read('CaptureInbox/29868_1.partial.recovered'),raw)
                self.assertEqual(z.read('CaptureInbox/29868_1.partial.population'),b'KUFPOP01')
                self.assertEqual(z.read('Replays/selected.kufreplay'),selected.read_bytes())
                self.assertEqual(z.read('Logs/engine.log'),b'MS01 evidence')
                self.assertFalse(any('engine-local.key' in n or n.endswith('.exe') for n in z.namelist()))
                info=json.loads(z.read('bundle.json'))
                self.assertEqual(info['skipped'],[])
                for entry in info['included']:
                    self.assertEqual(entry['sha256'],hashlib.sha256(z.read(entry['name'])).hexdigest())

    def test_zip_limits_report_omissions_and_rejects_manifest_path_escape(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);inbox=root/'CaptureInbox';inbox.mkdir()
            (inbox/'1_1.partial.recovered').write_bytes(b'x'*100)
            (inbox/'1_1.partial.population').write_bytes(b'KUFPOP01')
            replay=root/'bad.kufreplay'
            with zipfile.ZipFile(replay,'w') as z:
                z.writestr('manifest.json',json.dumps({'recovery':{'source':'../engine-local.key'}}))
            with patch('diagnostics.REPORT_FILE_LIMIT',20):
                archive=create_report_zip(root,replay=replay)
            with zipfile.ZipFile(archive) as z:
                self.assertNotIn('CaptureInbox/1_1.partial.recovered',z.namelist())
                self.assertEqual(z.read('CaptureInbox/1_1.partial.population'),b'KUFPOP01')
                info=json.loads(z.read('bundle.json'))
                self.assertTrue(any(e['name']=='CaptureInbox/1_1.partial.recovered' for e in info['skipped']))
                self.assertFalse(any('..' in n or 'engine-local.key' in n for n in z.namelist()))

    def test_zip_empty_store_and_missing_related_raw_are_explicit(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            with zipfile.ZipFile(create_report_zip(root)) as z:
                self.assertEqual(json.loads(z.read('bundle.json'))['included'],[])
            replay=root/'old.kufreplay'
            with zipfile.ZipFile(replay,'w') as z:
                z.writestr('manifest.json',json.dumps({'recovery':{'source':'9_1.partial'}}))
            with zipfile.ZipFile(create_report_zip(root,replay=replay)) as z:
                self.assertTrue(any(e['name']=='CaptureInbox/9_1.partial' for e in json.loads(z.read('bundle.json'))['skipped']))
            (root/'CaptureInbox').mkdir()
            (root/'CaptureInbox/9_1.partial.population').write_bytes(b'KUFPOP01')
            with zipfile.ZipFile(create_report_zip(root,replay=replay)) as z:
                self.assertTrue(any(e['name']=='CaptureInbox/9_1.partial' for e in json.loads(z.read('bundle.json'))['skipped']))
                self.assertEqual(z.read('CaptureInbox/9_1.partial.population'),b'KUFPOP01')

    def test_report_retains_latest_large_log_tail(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'engine.log').write_bytes(b'x'*(1024*1024)+b'LATEST SYNC ERROR')
            report=create_report(root)
            self.assertEqual((report/'engine.log').stat().st_size,1024*1024)
            self.assertTrue((report/'engine.log').read_bytes().endswith(b'LATEST SYNC ERROR'))
            self.assertIn('engine.log',json.loads((report/'report.json').read_text())['logs_kept_last_1MiB'])

    def test_report_preserves_old_log_and_excludes_capture_and_key(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'engine.log').write_text('first session')
            archive_engine_log(root);archive_engine_log(root)
            self.assertEqual(len(list((root/'Errors/History').glob('*.log'))),1)
            (root/'engine.log').write_text('second session')
            inbox=root/'CaptureInbox';inbox.mkdir()
            (inbox/'1.partial.complete.rejected.json').write_text('{"error":"bad capture"}')
            (inbox/'1.partial.complete.rejected').write_bytes(b'raw game data')
            (root/'engine-local.key').write_bytes(b'secret key')
            log_error(root,'capture-validation','bad capture')
            report=create_report(root)
            self.assertEqual((report/'engine.log').read_text(),'second session')
            self.assertEqual(next(report.glob('engine-*.log')).read_text(),'first session')
            self.assertTrue((report/'1.partial.complete.rejected.json').exists())
            self.assertFalse((report/'engine-local.key').exists())
            self.assertFalse((report/'1.partial.complete.rejected').exists())
            self.assertIn('bad capture',(report/'launcher-errors.log').read_text(encoding='utf-8'))

    def test_report_without_game_run(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue((create_report(d)/'에러코드.txt').exists())

    def test_build_identity_in_report_and_missing_file(self):
        with tempfile.TemporaryDirectory() as d:
            game=Path(d)/'game.exe';game.write_bytes(b'MZfixture')
            identity=game_identity(game)
            self.assertEqual(identity['sha256'],hashlib.sha256(game.read_bytes()).hexdigest())
            report=create_report(Path(d)/'data',game)
            self.assertIn(identity['sha256'],(report/'game-build.json').read_text())
            game.unlink()
            self.assertIn('error',game_identity(game))

    def test_only_explicit_game_builds_are_supported(self):
        self.assertTrue(supported_game_hash(GAME_HASH))
        self.assertTrue(supported_game_hash(UPDATED_GAME_HASH))
        self.assertFalse(supported_game_hash('0'*64))
        self.assertFalse(supported_game_hash(None))

    def test_unknown_build_is_diagnosed_before_injection(self):
        with tempfile.TemporaryDirectory() as d:
            game=Path(d)/'KingdomUnderFire.exe';game.write_bytes(b'MZunsupported-build')
            store=Store(Path(d)/'data')
            with patch('engine.subprocess.run') as run:
                with self.assertRaisesRegex(ValueError,'GB01'):
                    launch_engine(game,store)
                run.assert_not_called()
            self.assertIn(hashlib.sha256(game.read_bytes()).hexdigest(),
                (store.root/'Errors/game-build.json').read_text())
