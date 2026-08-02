#!/usr/bin/env python3
"""Moved to megaflash-vm — thin forwarder for old paths / muscle memory."""
from __future__ import annotations

import os
import runpy
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
mfvm = Path(os.environ.get("MEGAFLASH_VM_ROOT", root / ".." / "megaflash-vm")).resolve()
target = mfvm / "scripts" / "test-xmodem-upload.py"
if not target.is_file():
    sys.stderr.write(f"test-xmodem-upload.py moved to megaflash-vm: {target}\n")
    sys.exit(1)
sys.argv[0] = str(target)
runpy.run_path(str(target), run_name="__main__")
