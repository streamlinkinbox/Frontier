"""Validate the committed evidence bundle (no device / extra Python packages required)."""

import hashlib
import json
import struct
import tarfile
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit

root = Path(__file__).resolve().parents[1]


class Links(HTMLParser):
    def handle_starttag(self, tag, attrs):
        for k, v in attrs:
            if k not in ["src", "href"] or not v:
                continue
            u = urlsplit(v)
            if u.scheme or not u.path:
                continue
            assert (root / u.path).exists(), v


Links().feed((root / "index.html").read_text())
for p in (root / "frames").glob("*.png"):
    raw = p.read_bytes()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack_from(">II", raw, 16) == (
        480,
        270,
    ), p.name
with tarfile.open(root / "replay/native-inputs.tar.gz") as inputs:
    for mode in [
        "native-cap",
        "native-gi",
        "native-local",
        "native-clip",
        "native-hybrid",
        "packet",
        "lamps-fast",
    ]:
        for line in inputs.extractfile(mode + "/bindings").read().decode().splitlines():
            if line.startswith("T "):
                _, name, unit, target, internal, w, h, d, fmt, filt = line.split()
                size = int(w) * int(h) * int(d) * (4 if int(fmt) == 6403 else 16)
                assert (
                    len(inputs.extractfile(mode + "/" + name + ".f32").read()) == size
                )
checks = json.loads((root / "checks.json").read_text())
bench = json.loads((root / "benchmark.json").read_text())
indirect = json.loads((root / "floor-bounce.json").read_text())
assert checks["packet"]["partial_rmse"] < checks["native-cap"]["partial_rmse"]
assert bench["median_ms"]["packet"] < bench["median_ms"]["native-cap"]
assert bench["sha256"]["packet"] == checks["packet"]["sha256"]
assert checks["null"]["display"]["max"] == 0
assert checks["emission"]["display"]["max"] > 0
assert indirect["off"]["linear_max"] == 0 < indirect["emission"]["linear_max"]
assert indirect["combined_linearity_max_abs"] < 1.0e-5
for line in (root / "SHA256SUMS").read_text().splitlines():
    h, name = line.split("  ", 1)
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == h, name
print(
    "PASS: gallery links, PNG extents, fixture texture sizes, numeric checks and evidence digests"
)
