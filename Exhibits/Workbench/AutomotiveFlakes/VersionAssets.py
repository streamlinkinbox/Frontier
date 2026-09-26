#!/usr/bin/env python3
"""Keep the gallery's JavaScript and shader cache keys tied to their contents."""
from pathlib import Path
import hashlib
import re

GALLERY = Path(__file__).resolve().parents[2] / 'Gallery/AutomotiveFlakes'

def version_assets():
    shader_hash = hashlib.sha256((GALLERY / 'shared.glsl').read_bytes()).hexdigest()
    script = GALLERY / 'paint.js'
    text, count = re.subn(r"const shaderRevision='[^']*';", f"const shaderRevision='{shader_hash}';", script.read_text())
    if count != 1:
        raise RuntimeError('Missing or duplicate shader revision declaration')
    script.write_text(text)
    script_hash = hashlib.sha256(script.read_bytes()).hexdigest()
    index = GALLERY / 'index.html'
    text, count = re.subn(r'src="paint\.js(?:\?[^\"]*)?"', f'src="paint.js?v={script_hash}"', index.read_text())
    if count != 1:
        raise RuntimeError('Missing or duplicate paint script reference')
    index.write_text(text)

if __name__ == '__main__':
    version_assets()
