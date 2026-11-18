"""Build the native mesh-emission pass inputs from the same moving instance and FVB1 triangles."""

import shutil
import sys
from pathlib import Path

import numpy as np

src, dst, bundle = map(Path, sys.argv[1:])
shutil.copytree(src, dst, dirs_exist_ok=True)
b = bundle.read_bytes()
tri = np.frombuffer(b, "<f4", count=2960 * 16, offset=84 + 5930 * 32).reshape(-1, 16)
t = tri[np.isin(tri[:, 3], [12, 13, 14])]
M = np.fromfile(src / "uInstances.f32", "<f4")[:16].reshape(4, 4).T
rows = np.zeros((len(t), 4, 4), "<f4")
rows[:, 0, :3] = t[:, :3] @ M[:3, :3].T + M[:3, 3]
rows[:, 1, :3] = t[:, 4:7] @ M[:3, :3].T
rows[:, 2, :3] = t[:, 8:11] @ M[:3, :3].T
rows[:, 3, :3] = np.array([[2.2, 2.6, 3.2], [7, 7.6, 8.8], [6.5, 0.4, 0.3]])[
    t[:, 3].astype(int) - 12
]
rows.tofile(dst / "uLampFaces.f32")
p = dst / "bindings"
p.write_text(p.read_text() + f"T uLampFaces 5 3553 34836 4 {len(t)} 1 6408 9728\n")
p = dst / "pass.frag"
s = p.read_text()
p.write_text(
    s[: s.rindex("void main()")]
    + Path(__file__).with_name("LampTransfer.glsl").read_text()
)
# A visibility ray needs any blocker, not its distance. Preserve all epsilon and
# glass rules, but terminate at the first valid triangle. Do not cap node visits.
s = (
    p.read_text()
    .replace("for (int stepIdx = 0; stepIdx < 240; stepIdx++) {", "while (depth > 0) {")
    .replace("{ closestBlocker = travel; blocked = true; }", "{ return travel; }")
)
p.write_text(s)
