"""The UDP worker must keep receiving when the display consumes frames slowly."""
import queue
import socket
import unittest

from test_udp import RAW, packets, r


class QueuedSocket:
    def __init__(self):
        self.packets = queue.Queue()

    def recvfrom(self, size):
        try:
            item = self.packets.get(timeout=0.02)
        except queue.Empty:
            raise socket.timeout()
        if isinstance(item, Exception):
            raise item
        packet, source = item
        return packet[:size], source

    def frame(self, frame_id, source=("127.0.0.1", 8000)):
        for packet in packets(frame_id):
            self.packets.put((packet, source))


class TestReceiveWorker(unittest.TestCase):
    def run_worker(self, sock, **options):
        worker = r.ReceiveWorker(sock, **options)
        worker.start()
        worker.join(timeout=3)
        if worker.is_alive():
            worker.stop_event.set()
            worker.join()
            self.fail("receive worker did not reach its frame limit")
        return worker

    def test_slow_display_keeps_only_latest_frame(self):
        sock = QueuedSocket()
        for frame_id in range(4):
            sock.frame(frame_id)
        worker = self.run_worker(sock, frame_limit=4)
        frame, stats = worker.take()
        self.assertEqual(frame, (3, RAW))
        self.assertEqual(stats[:3], (4, 0, 0))
        self.assertTrue(stats[4])
        self.assertIsNone(worker.take()[0])

    def test_source_filter_and_active_peer_lock(self):
        sock = QueuedSocket()
        sock.frame(100, ("127.0.0.2", 8000))
        sock.frame(1)
        sock.frame(100, ("127.0.0.1", 9000))
        sock.frame(2)
        worker = self.run_worker(sock, source="127.0.0.1", frame_limit=2)
        frame, stats = worker.take()
        self.assertEqual(frame, (2, RAW))
        self.assertEqual(stats[:3], (2, 0, 0))

    def test_receive_error_is_reported(self):
        sock = QueuedSocket()
        sock.packets.put(OSError("test socket failure"))
        worker = self.run_worker(sock)
        self.assertTrue(worker.done)
        self.assertEqual(worker.error, "test socket failure")


if __name__ == "__main__":
    unittest.main()
