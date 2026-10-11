import queue
import sys
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from launcher import Launcher


class LauncherQueueTests(unittest.TestCase):
    def test_busy_background_does_not_block_or_duplicate_user_launch(self):
        app = Launcher.__new__(Launcher)
        app.busy = True
        app.work_key = 'collect'
        app.pending = []
        app.status = SimpleNamespace(set=lambda value: None)
        app.work(lambda: None, lambda result: None, key='collect', background=True)
        launch = lambda: 123
        app.work(launch, lambda result: None, key='launch')
        app.work(launch, lambda result: None, key='launch')
        self.assertEqual(len(app.pending), 1)
        self.assertIs(app.pending[0][0], launch)

    def test_completion_starts_queued_operation(self):
        app = Launcher.__new__(Launcher)
        app.busy = True
        app.work_key = 'collect'
        app.events = queue.Queue()
        app.status = SimpleNamespace(set=lambda value: None)
        app.root = SimpleNamespace(after=lambda *args: None)
        app.store = SimpleNamespace(replays=Path('fixture'))
        ran = threading.Event()
        app.pending = [(lambda: (ran.set(), 456)[1], lambda result: None, 'launch')]
        completed = []
        app.events.put((completed.append, 'saved', None))
        app.poll()
        self.assertTrue(ran.wait(2))
        self.assertEqual(completed, ['saved'])
        self.assertEqual(app.work_key, 'launch')
        self.assertEqual(app.events.get(timeout=2)[1], 456)
