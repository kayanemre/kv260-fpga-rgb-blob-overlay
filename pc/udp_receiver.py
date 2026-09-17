#!/usr/bin/env python3
"""KV260 blob overlay: 640x480 RGB888 UDP video, !IHHH packet headers."""
import argparse
from collections import OrderedDict, deque
import socket
import struct
import threading
import time

WIDTH, HEIGHT = 640, 480
FRAME_BYTES = WIDTH * HEIGHT * 3
PAYLOAD = 1200
TOTAL_PACKETS = (FRAME_BYTES + PAYLOAD - 1) // PAYLOAD
HEADER = struct.Struct("!IHHH")
WINDOW = "KV260 blob overlay"


def newer(a, b):
    return 0 < ((a - b) & 0xFFFFFFFF) < 0x80000000


class Reassembler:
    def __init__(self, timeout=0.5, max_frames=3):
        self.timeout = timeout
        self.max_frames = max_frames
        self.pending = OrderedDict()
        self.retired = deque(maxlen=64)
        self.last_complete = None
        self.completed = self.dropped = self.invalid = 0

    def discard(self, frame):
        del self.pending[frame]
        self.retired.append(frame)
        self.dropped += 1

    def expire(self, now):
        for frame, state in list(self.pending.items()):
            if now - state[0] >= self.timeout:
                self.discard(frame)

    def feed(self, packet, now=None):
        now = time.monotonic() if now is None else now
        self.expire(now)
        if len(packet) < HEADER.size:
            self.invalid += 1
            return None
        frame, index, total, length = HEADER.unpack_from(packet)
        expected_length = min(PAYLOAD, FRAME_BYTES - index * PAYLOAD)
        if (total != TOTAL_PACKETS or index >= TOTAL_PACKETS or
                length != expected_length or len(packet) != HEADER.size + length):
            self.invalid += 1
            return None
        if frame in self.retired or (self.last_complete is not None and
                                    not newer(frame, self.last_complete)):
            return None
        if frame not in self.pending:
            if len(self.pending) >= self.max_frames:
                self.discard(next(iter(self.pending)))
            self.pending[frame] = [now, bytearray(FRAME_BYTES), bytearray(TOTAL_PACKETS), 0]
        state = self.pending[frame]
        offset = index * PAYLOAD
        payload = packet[HEADER.size:]
        if state[2][index]:
            if state[1][offset:offset + length] != payload:
                self.invalid += 1
                self.discard(frame)
            return None
        state[1][offset:offset + length] = payload
        state[2][index] = 1
        state[3] += 1
        if state[3] != TOTAL_PACKETS:
            return None
        result = bytes(state[1])
        del self.pending[frame]
        self.last_complete = frame
        self.completed += 1
        for old in list(self.pending):
            if not newer(old, frame):
                self.discard(old)
        return frame, result


class ReceiveWorker(threading.Thread):
    """Drain UDP independently of GUI work; retain only the newest complete frame."""
    def __init__(self, sock, timeout=0.5, source=None, frame_limit=0):
        super().__init__(name="udp-receive", daemon=True)
        self.sock = sock
        self.timeout = timeout
        self.source = source
        self.frame_limit = frame_limit
        self.condition = threading.Condition()
        self.stop_event = threading.Event()
        self.latest = None
        self.completed = self.dropped = self.invalid = 0
        self.last_frame = time.monotonic()
        self.done = False
        self.error = None

    def take(self, wait=0.02):
        with self.condition:
            if self.latest is None and not self.done:
                self.condition.wait(wait)
            result, self.latest = self.latest, None
            return result, (self.completed, self.dropped, self.invalid, self.last_frame,
                            self.done, self.error)

    def run(self):
        assembler = Reassembler(self.timeout)
        peer = None
        last_packet = next_stats = 0.0
        base_dropped = base_invalid = 0
        try:
            while not self.stop_event.is_set():
                try:
                    # An extra byte ensures an oversized datagram cannot look valid.
                    packet, source = self.sock.recvfrom(HEADER.size + PAYLOAD + 1)
                except socket.timeout:
                    packet = None
                now = time.monotonic()
                result = None
                if packet is None:
                    assembler.expire(now)
                else:
                    if self.source and source[0] != self.source:
                        continue
                    if peer is not None and source != peer and now - self.last_frame < 2:
                        continue
                    if peer is not None and (source != peer or now - last_packet >= 2):
                        # A restarted sender can begin at frame 0, even on the same port.
                        base_dropped += assembler.dropped + len(assembler.pending)
                        base_invalid += assembler.invalid
                        assembler = Reassembler(self.timeout)
                        peer = None
                    invalid_before = assembler.invalid
                    result = assembler.feed(packet, now)
                    if assembler.invalid == invalid_before and (assembler.pending or result):
                        peer = source
                        last_packet = now
                if result or now >= next_stats:
                    with self.condition:
                        self.dropped = base_dropped + assembler.dropped
                        self.invalid = base_invalid + assembler.invalid
                        if result:
                            self.latest = result
                            self.completed += 1
                            self.last_frame = now
                        self.condition.notify_all()
                    next_stats = now + 0.1
                if self.frame_limit and self.completed >= self.frame_limit:
                    break
        except Exception as error:
            with self.condition:
                self.error = str(error)
        finally:
            with self.condition:
                self.dropped = base_dropped + assembler.dropped
                self.invalid = base_invalid + assembler.invalid
                self.done = True
                self.condition.notify_all()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--source", help="accept only this sender IPv4 address")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--snapshot", metavar="PATH", help="save one complete received frame (before PC FPS text), also in headless mode")
    parser.add_argument("--frames", type=int, default=0, help="exit after N complete frames")
    parser.add_argument("--timeout", type=float, default=0.5, help="incomplete-frame expiry seconds")
    parser.add_argument("--idle-exit", type=float, default=0, help="exit with failure after N seconds without a complete frame")
    args = parser.parse_args()
    if not 0 < args.port <= 65535 or args.timeout <= 0 or args.frames < 0 or args.idle_exit < 0:
        parser.error("invalid port, timeout, frame count, or idle timeout")
    if not args.headless or args.snapshot:
        import cv2
        import numpy as np
    if not args.headless:
        waiting = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)
        cv2.putText(waiting, "Waiting for KV260 UDP...", (30, 240), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255,255,255), 1)
        cv2.imshow(WINDOW, waiting)
    start = report = time.monotonic()
    reported_complete = reported_displayed = displayed = 0
    fps = display_fps = 0.0
    status = 0
    snapshot_saved = False
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
        sock.bind((args.bind, args.port))
        sock.settimeout(0.02)
        buffer_bytes = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
        print(f"Listening {args.bind}:{args.port}, RGB888 {WIDTH}x{HEIGHT}, "
              f"socket receive buffer={buffer_bytes} bytes", flush=True)
        worker = ReceiveWorker(sock, args.timeout, args.source, args.frames)
        worker.start()
        try:
            while True:
                result, stats = worker.take(0.02 if not args.headless else 0.25)
                complete, dropped, invalid, last_frame, done, error = stats
                now = time.monotonic()
                if result and (not args.headless or (args.snapshot and not snapshot_saved)):
                    frame_id, raw = result
                    rgb = np.frombuffer(raw, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)
                    bgr = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
                    if args.snapshot and not snapshot_saved:
                        try:
                            if not cv2.imwrite(args.snapshot, bgr):
                                raise OSError("image writer returned failure")
                        except (OSError, cv2.error) as error:
                            print(f"Cannot save snapshot {args.snapshot}: {error}", flush=True)
                            status = 1
                            break
                        snapshot_saved = True
                        print(f"Snapshot frame_id={frame_id} saved to {args.snapshot}", flush=True)
                if result and not args.headless:
                    cv2.putText(bgr, f"RX {fps:.1f} / display {display_fps:.1f} FPS", (12, 28),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255,255,255), 1)
                    cv2.imshow(WINDOW, bgr)
                    displayed += 1
                if now - report >= 1:
                    fps = (complete - reported_complete) / (now - report)
                    display_fps = (displayed - reported_displayed) / (now - report)
                    print(f"RX {fps:.2f} FPS display={display_fps:.2f} FPS complete={complete} "
                          f"incomplete={dropped} invalid={invalid}", flush=True)
                    reported_complete, reported_displayed = complete, displayed
                    report = now
                if not args.headless and cv2.waitKey(1) & 0xff in (27, ord('q')):
                    break
                if done:
                    if error:
                        print(f"UDP receive failed: {error}", flush=True)
                        status = 1
                    break
                if args.idle_exit and now - last_frame >= args.idle_exit:
                    print("No complete frame before idle deadline", flush=True)
                    status = 1
                    break
        except KeyboardInterrupt:
            pass
        finally:
            worker.stop_event.set()
            worker.join()
            if not args.headless:
                cv2.destroyAllWindows()
    elapsed = time.monotonic() - start
    print(f"Complete={worker.completed} average={worker.completed/max(elapsed,1e-9):.2f} FPS "
          f"displayed={displayed} incomplete={worker.dropped} invalid={worker.invalid}", flush=True)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
