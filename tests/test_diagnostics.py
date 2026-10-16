import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from diagnostics import archive_engine_log,create_report,log_error
from diagnostics import game_identity
from engine import supported_game_hash, GAME_HASH, UPDATED_GAME_HASH
from engine import launch_engine
from replay_store import Store
from unittest.mock import patch
import hashlib

class DiagnosticsTests(unittest.TestCase):
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
