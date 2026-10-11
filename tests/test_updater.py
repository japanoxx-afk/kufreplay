import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
import updater

class UpdaterTests(unittest.TestCase):
    def release(self, checksum):
        prefix = 'https://github.com/japanoxx-afk/kufreplay/releases/download/v0.2.0/'
        return json.dumps({'tag_name':'v0.2.0', 'assets':[
            {'name':updater.ASSET, 'browser_download_url':prefix + updater.ASSET},
            {'name':updater.ASSET + '.sha256', 'browser_download_url':prefix + updater.ASSET + '.sha256'}]}).encode()

    def test_verified_staged_download(self):
        payload = b'MZtest-fixture-only'
        digest = hashlib.sha256(payload).hexdigest().encode()
        with tempfile.TemporaryDirectory() as root, patch.object(updater, 'read_url', side_effect=[self.release(digest), digest, payload]):
            p = updater.download_latest(root)
            self.assertEqual(p.read_bytes(), payload)
            self.assertEqual(list(Path(root).rglob('*.partial')), [])

    def test_tampered_download_never_committed(self):
        with tempfile.TemporaryDirectory() as root, patch.object(updater, 'read_url', side_effect=[self.release(''), b'a'*64, b'MZtampered']):
            with self.assertRaises(ValueError):
                updater.download_latest(root)
            self.assertEqual(list(Path(root).rglob('*.exe')), [])

    def test_current_version_no_download(self):
        with patch.object(updater, 'read_url', return_value=b'{"tag_name":"v0.1.0"}') as call:
            self.assertIsNone(updater.download_latest('unused'))
            self.assertEqual(call.call_count, 1)

    def test_untrusted_host_rejected_before_request(self):
        with patch.object(updater.urllib.request, 'urlopen') as request:
            with self.assertRaises(ValueError):
                updater.read_url('https://evil.example/launcher.exe', 100)
            request.assert_not_called()

if __name__ == '__main__':
    unittest.main()
