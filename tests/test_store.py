import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from replay_store import Store, validate, REQUIRED
from updater import version_tuple

def fixture(path, when='2026-10-11T12:00:00+09:00', extra=None, corrupt=False, complete=True):
    contents = {name: (name + ' fixture data').encode() for name in REQUIRED}
    hashes = {name: hashlib.sha256(value).hexdigest() for name, value in contents.items()}
    m = {'format_version':1, 'complete':complete, 'game_sha256':'a'*64, 'rules_sha256':'b'*64,
         'map_sha256':hashes['map.bin'], 'game_version':'fixture', 'rules_version':'fixture',
         'adapter_id':'test-only', 'recorded_at':when, 'map_id':'test', 'ticks':120, 'tick_rate':30,
         'hashes':hashes}
    if corrupt:
        contents['commands.bin'] = b'corrupt'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(m))
        for name, value in contents.items():
            z.writestr(name, value)
        if extra:
            z.writestr(extra, b'bad')

class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / 'fixture.zip'
        self.store = Store(self.root / 'library')

    def tearDown(self):
        self.temp.cleanup()

    def test_retention_permanent_and_settings_restart(self):
        self.store.set_keep(2)
        fixture(self.source, when='2026-10-01T12:00:00+09:00')
        pinned = self.store.import_file(self.source)
        for day in (2,3,4):
            fixture(self.source, when=f'2026-10-0{day}T12:00:00+09:00')
            self.store.import_file(self.source, permanent=False)
        self.assertTrue(pinned.exists())
        self.assertEqual(len(self.store.listing()), 3)
        resumed = Store(self.store.root)
        self.assertEqual(resumed.config['keep'], 2)
        self.assertEqual(len(resumed.listing()), 3)

    def test_hostile_paths_and_embedded_executables(self):
        for name in ('../outside', '/absolute', 'C:/evil', 'engine.exe'):
            fixture(self.source, extra=name)
            with self.assertRaises(ValueError):
                self.store.import_file(self.source)
        self.assertEqual(self.store.listing(), [])

    def test_integrity_and_incomplete(self):
        for opts in ({'corrupt':True}, {'complete':False}):
            fixture(self.source, **opts)
            with self.assertRaises(ValueError):
                self.store.import_file(self.source)
        self.assertEqual(list(self.store.replays.glob('*.partial')), [])

    def test_partial_is_not_listed(self):
        fixture(self.store.replays / 'unfinished.partial')
        self.assertEqual(self.store.listing(), [])

    def test_duplicate_archive_member(self):
        fixture(self.source)
        with zipfile.ZipFile(self.source, 'a') as z:
            z.writestr('map.bin', b'duplicate')
        with self.assertRaises(ValueError):
            validate(self.source)

    def test_copy_migration_preserves_source(self):
        folder = self.root / 'old'
        folder.mkdir()
        source = folder / 'old.kufreplay'
        fixture(source)
        self.assertEqual(self.store.migrate(folder), 1)
        self.assertTrue(source.exists())
        self.assertTrue(self.store.listing()[0][2])

    def test_bad_retention(self):
        for count in (0, -1, 10001):
            with self.assertRaises(ValueError):
                self.store.set_keep(count)

    def test_decompression_bomb(self):
        fixture(self.source)
        # Create a valid member list with a member whose compressed ratio is excessive.
        with zipfile.ZipFile(self.source) as z:
            contents = {n:z.read(n) for n in z.namelist()}
        contents['commands.bin'] = b'0' * (2 * 1024 * 1024)
        with zipfile.ZipFile(self.source, 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in contents.items():
                z.writestr(name, data)
        with self.assertRaisesRegex(ValueError, '압축률'):
            validate(self.source)

    def test_version(self):
        self.assertGreater(version_tuple('v0.2.0'), version_tuple('0.1.0'))
        with self.assertRaises(ValueError):
            version_tuple('../../malicious')

if __name__ == '__main__':
    unittest.main()
