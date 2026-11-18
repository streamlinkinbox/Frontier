"""Make a replay fixture with input textures only. Hard-link identical members."""

import hashlib
import io
import sys
import tarfile
from pathlib import Path

root = Path(sys.argv[1])
seen = {}
with tarfile.open(sys.argv[2], "w:gz", compresslevel=6) as tar:
    for name in [
        "native-local",
        "native-clip",
        "native-hybrid",
        "native-cap",
        "native-gi",
        "packet",
        "lamps-fast",
    ]:
        for p in sorted((root / name).iterdir()):
            data = p.read_bytes()
            key = hashlib.sha256(data).digest()
            info = tarfile.TarInfo(str(p.relative_to(root)))
            info.mode = 0o644
            if key in seen:
                info.type = tarfile.LNKTYPE
                info.linkname = seen[key]
                tar.addfile(info)
            else:
                seen[key] = info.name
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
