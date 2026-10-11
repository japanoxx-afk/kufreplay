import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from steam_launch import launch_steam_game

class SteamLaunchTests(unittest.TestCase):
    def test_steam_protocol_instead_of_direct_executable(self):
        with patch('steam_launch.os.startfile') as start:
            self.assertEqual(launch_steam_game(), 'steam://rungameid/2183600')
            start.assert_called_once_with('steam://rungameid/2183600')

    def test_protocol_failure_is_reported(self):
        with patch('steam_launch.os.startfile', side_effect=OSError('no handler')):
            with self.assertRaisesRegex(ValueError, 'Steam'):
                launch_steam_game()
