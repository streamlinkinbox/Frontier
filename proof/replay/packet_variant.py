"""Specialize the captured single-sun pass; reuse all original pass bindings."""

import shutil
import sys
from pathlib import Path

src, dst = map(Path, sys.argv[1:])
shutil.copytree(src, dst, dirs_exist_ok=True)
p = dst / "pass.frag"
s = (
    p.read_text()
    .replace(
        "float evalBvhLight(",
        Path(__file__).with_name("SunPacket.glsl").read_text()
        + "\nfloat evalBvhLight(",
    )
    .replace(
        "  vec3 origin = pos + nrm * 0.004;",
        "  vec3 origin = pos + nrm * 0.004;\n  if(uLightType[idx]==0)return sunPacket(origin,toLight,uLightRadius[idx],blkOut);",
    )
)
p.write_text(s)
