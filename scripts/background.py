#!/usr/bin/env python3
"""Launch build jobs detached, with logs and PID files in build/."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('job', choices=['software', 'sim', 'fpga'])
a = p.parse_args()
(root/'build').mkdir(exist_ok=True)
pidfile = root/'build'/f'{a.job}.pid'
if pidfile.exists():
    try:
        pid = int(pidfile.read_text())
        os.kill(pid, 0)
        cmdline = Path(f'/proc/{pid}/cmdline').read_bytes()
        if b'scripts/' in cmdline:
            raise SystemExit(f'{a.job} already running, PID {pid}')
    except (ProcessLookupError, FileNotFoundError):
        pass
script = {'software': 'build_software.sh', 'sim': 'simulate.sh', 'fpga': 'build_fpga.sh'}[a.job]
with (root/'build'/f'{a.job}_console.log').open('w') as log:
    child = subprocess.Popen(['bash', f'scripts/{script}'], cwd=root,
        stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
pidfile.write_text(str(child.pid)+'\n')
print(f'{a.job}: PID={child.pid}, log=build/{a.job}_console.log')
