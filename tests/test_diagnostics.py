import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from diagnostics import archive_engine_log,create_report,log_error

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
