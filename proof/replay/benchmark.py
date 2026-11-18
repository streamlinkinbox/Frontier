"""Warm synchronized GLES draw latency. No uploads, readback, or swapchain in timed region."""

import hashlib
import json
import statistics
import subprocess
import sys
from pathlib import Path

exe, fixtures, out = map(Path, sys.argv[1:])
out.mkdir(exist_ok=True)
order = ["native-cap", "packet", "native-gi", "lamps-fast"]
report = {"order": order + order[::-1], "warm_ms": {}, "sha256": {}}
for name in report["order"]:
    f = out / (name + ".bench.raw")
    log = subprocess.check_output([exe, fixtures / name, f, "3"], text=True)
    ms = list(map(float, log.split("warm-pass-ms")[1].split()))
    report["renderer"] = log.splitlines()[0]
    report["warm_ms"].setdefault(name, []).extend(ms)
    h = hashlib.sha256(f.read_bytes()).hexdigest()
    assert report["sha256"].setdefault(name, h) == h
    print(name, ms, flush=True)
report["median_ms"] = {k: statistics.median(v) for k, v in report["warm_ms"].items()}
(out / "benchmark.json").write_text(json.dumps(report, indent=2) + "\n")
