import tempfile
import unittest
from pathlib import Path
from activity import Activity


class V5Checks(unittest.TestCase):
    def test_pause_keeps_segment_stop_starts_new_segment(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = [1000.0]
            activity = Activity(Path(directory)/'activity.db', lambda: clock[0])
            self.assertTrue(activity.snapshot()['stopped'])
            activity.command('STUDY')
            clock[0] += 10.6
            activity.command('PAUSE')
            clock[0] += 300
            activity.command('RESUME')
            clock[0] += 0.6
            self.assertEqual(activity.snapshot()['elapsed'], 11)
            activity.command('STOP')
            self.assertTrue(activity.snapshot()['stopped'])
            activity.command('STUDY')
            self.assertEqual(activity.snapshot()['elapsed'], 0)
            self.assertAlmostEqual(activity.snapshot()['totals']['STUDY'], 11.2)
            clock[0] += 5
            activity.command('STOP')
            activity.command('RESUME')
            self.assertEqual(activity.snapshot()['elapsed'], 0)
            self.assertAlmostEqual(activity.snapshot()['totals']['STUDY'], 16.2)
            activity.close()

    def test_rest_and_focus_wire_packets(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = [1000.0]
            activity = Activity(Path(directory)/'activity.db', lambda: clock[0])
            activity.command('REST')
            clock[0] += 125
            activity.command('REST')
            self.assertEqual(activity.snapshot()['elapsed'], 125)
            self.assertIn(b'M:R:2', activity.packets())
            self.assertIn(b'M:U:4,0,1', activity.packets())
            activity.command('FOCUS')
            clock[0] += 60
            self.assertEqual(activity.snapshot()['elapsed'], 185)
            self.assertAlmostEqual(activity.snapshot()['deep_seconds'], 60)
            self.assertTrue(all(len(packet)<=20 for packet in activity.packets()))
            activity.close()


if __name__ == '__main__':
    unittest.main()
