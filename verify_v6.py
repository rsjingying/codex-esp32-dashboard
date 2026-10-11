from datetime import datetime
import tempfile
import unittest
from pathlib import Path
from local_clock import clock_packet
from activity import Activity


class V6Checks(unittest.TestCase):
    def test_local_clock_at_midnight_and_last_second(self):
        self.assertEqual(clock_packet(datetime(2026,10,11,23,59,59)),b'M:K:20261011,86399')
        self.assertEqual(clock_packet(datetime(2026,10,12)),b'M:K:20261012,0')
        self.assertLessEqual(len(clock_packet(datetime(2026,10,11,23,59,59))),20)

    def test_deep_focus_total_while_activity_is_paused(self):
        with tempfile.TemporaryDirectory() as directory:
            now=[1000.0]
            activity=Activity(Path(directory)/'activity.db',lambda:now[0])
            activity.command('STUDY')
            activity.command('DEEP1')
            now[0]+=125
            activity.command('PAUSE')
            now[0]+=60
            self.assertIn(b'M:F:2',activity.packets())
            self.assertIn(b'M:U:2,1,0',activity.packets())
            self.assertTrue(all(len(packet)<=20 for packet in activity.packets()))
            activity.close()


if __name__=='__main__':
    unittest.main()
