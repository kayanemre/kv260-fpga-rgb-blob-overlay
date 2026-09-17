#!/usr/bin/env python3
"""Finish packaging and evidence after the already-running Vivado build exits."""
from pathlib import Path
import subprocess
import time
root=Path(__file__).resolve().parents[1]
end=time.monotonic()+7200
while time.monotonic()<end:
    status=(root/'build/fpga.status').read_text().strip()
    if status!='RUNNING':
        if status=='COMPLETE':
            result=subprocess.run(['bash','scripts/package.sh'],cwd=root)
            if result.returncode:
                (root/'build/package.status').write_text('FAILED\n')
            else:
                (root/'build/package.status').write_text('COMPLETE\n')
        subprocess.run(['python3','scripts/update_validation.py'],cwd=root,check=True)
        break
    time.sleep(5)
else:
    raise SystemExit('Finalizer timed out; inspect Vivado status manually')
