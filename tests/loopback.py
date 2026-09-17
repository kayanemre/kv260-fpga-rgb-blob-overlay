#!/usr/bin/env python3
"""Actual C++ sender -> UDP socket -> Python reassembler, byte-exact check."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import time

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('receiver', root/'pc/udp_receiver.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
colors = (bytes((255,0,0)), bytes((120,10,10)), bytes((0,255,0)),
          bytes((0,0,255)), bytes((255,255,255)), bytes((0,0,0)))
expected = b''.join(colors[x*6//r.WIDTH] for x in range(r.WIDTH))*r.HEIGHT
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4*1024*1024)
    sock.bind(('127.0.0.1', 0))
    sock.settimeout(1)
    cmd = [str(root/'build/camera_udp'), '--ip', '127.0.0.1', '--port', str(sock.getsockname()[1]),
           '--transport-test', '--frames', '5', '--fps', '10']
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    a = r.Reassembler()
    try:
        end = time.monotonic()+10
        while a.completed < 5 and time.monotonic() < end:
            try:
                packet, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            result = a.feed(packet)
            if result:
                assert result[1] == expected, 'C++ packet/header/pixel byte order mismatch'
        out, _ = p.communicate(timeout=3)
        print(out, end='')
        assert p.returncode == 0, 'sender failed'
        assert a.completed == 5, f'only {a.completed}/5 complete frames'
        print('PASS: C++ -> real UDP -> Python, 5 byte-exact RGB frames (transport only)')
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
