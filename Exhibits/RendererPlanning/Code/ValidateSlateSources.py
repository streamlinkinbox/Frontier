#============================================================================================================================================
#                                                        VALIDATESLATESOURCES.PY
#============================================================================================================================================
# 🧩 Verify the pinned reference copies without running imported apps or changing their original source.

import hashlib
import json
from pathlib import Path

Repository = Path(__file__).resolve().parents[3]
Manifest = json.loads((Repository / 'Exhibits/RendererPlanning/Results/SlateSources.json').read_text())
TotalBytes = 0
PaintingFiles = 0
for Entry in Manifest['entries']:
    Source = Repository / Entry['path']
    assert Source.resolve().is_relative_to((Repository / 'Experimental/SlateReference').resolve())
    Content = Source.read_bytes()
    assert hashlib.sha256(Content).hexdigest() == Entry['sha256'], Entry['path']
    Blob = b'blob ' + str(len(Content)).encode() + b'\0' + Content
    assert hashlib.sha1(Blob).hexdigest() == Entry['git_blob'], Entry['path']
    TotalBytes += len(Content)
    PaintingFiles += '/References/remix-remix-global-ui/' in Entry['path']
assert PaintingFiles == 30
print(f'PASS: {len(Manifest["entries"])} pinned source files, {TotalBytes} bytes, complete {PaintingFiles}-file painting UI app')
