import importlib.util
from pathlib import Path
import random
import unittest

spec = importlib.util.spec_from_file_location('receiver', Path(__file__).resolve().parents[1]/'pc/udp_receiver.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
RAW = bytes(range(256)) * (r.FRAME_BYTES // 256)


def packets(frame):
    return [r.HEADER.pack(frame, i, r.TOTAL_PACKETS, len(RAW[i*r.PAYLOAD:(i+1)*r.PAYLOAD])) +
            RAW[i*r.PAYLOAD:(i+1)*r.PAYLOAD] for i in range(r.TOTAL_PACKETS)]


class TestUDP(unittest.TestCase):
    def test_reordered_and_duplicate(self):
        a = r.Reassembler()
        p = packets(8)
        random.Random(7).shuffle(p)
        result = None
        for item in p:
            result = a.feed(item, 0) or result
            a.feed(item, 0)
        self.assertEqual(result, (8, RAW))
        self.assertEqual(a.completed, 1)

    def test_loss_timeout_and_next_frame(self):
        a = r.Reassembler()
        for item in packets(10)[1:]:
            self.assertIsNone(a.feed(item, 0))
        a.expire(1)
        self.assertEqual(a.dropped, 1)
        self.assertIsNone(a.feed(packets(10)[0], 1))
        for item in packets(11):
            result = a.feed(item, 1)
        self.assertEqual(result, (11, RAW))

    def test_malformed_and_conflicting_duplicate(self):
        a = r.Reassembler()
        for p in (b'x', r.HEADER.pack(1, 0, 1, 1200)+bytes(1200), packets(1)[0][:-1],
                  r.HEADER.pack(1, 65535, r.TOTAL_PACKETS, 1)+b'x'):
            self.assertIsNone(a.feed(p, 0))
        self.assertEqual(a.invalid, 4)
        p = packets(1)[0]
        a.feed(p, 0)
        a.feed(p[:-1]+bytes([p[-1]^1]), 0)
        self.assertEqual(a.dropped, 1)

    def test_bounded_storage_and_wraparound(self):
        a = r.Reassembler()
        for frame in range(20):
            a.feed(packets(frame)[0], 0)
        self.assertEqual(len(a.pending), 3)
        self.assertEqual(a.dropped, 17)
        a = r.Reassembler()
        for fid in (0xffffffff, 0):
            for p in packets(fid): result = a.feed(p, 0)
            self.assertEqual(result, (fid, RAW))
        self.assertIsNone(a.feed(packets(0xffffffff)[0], 0))

    def test_older_frame_cannot_replace_newer(self):
        a = r.Reassembler()
        p = packets(20)
        a.feed(p[0], 0)
        for item in packets(21): a.feed(item, 0)
        for item in p[1:]: self.assertIsNone(a.feed(item, 0))
        self.assertEqual(a.completed, 1)
        self.assertEqual(a.dropped, 1)


if __name__ == '__main__':
    unittest.main()
