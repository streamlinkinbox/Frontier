"""Restore the pinned vehicle demo dependency and its required lunar atlas."""
from pathlib import Path
import hashlib
import subprocess
import urllib.request

Root = Path(__file__).resolve().parents[2]
Vehicle = Root / 'Experimental' / 'VehicleHmiPrototype'
Atlas = Vehicle / 'Assets' / 'Celestial' / 'MoonAtlas.rgba.gz'
Expected = '9d224e34b5b10ef48dc73d89b743606efb668c44'


def VerifyAtlas(Contents):
    Header = f'blob {len(Contents)}\0'.encode()
    return hashlib.sha1(Header + Contents).hexdigest() == Expected


def RestoreAtlas():
    if Atlas.exists() and VerifyAtlas(Atlas.read_bytes()):
        return
    Address = f'https://api.github.com/repos/SultanAladin/Frontier/git/blobs/{Expected}'
    Request = urllib.request.Request(Address, headers={'Accept': 'application/vnd.github.raw', 'User-Agent': 'Frontier-fluid-reference'})
    with urllib.request.urlopen(Request, timeout=120) as Reply:
        Contents = Reply.read()
    if not VerifyAtlas(Contents):
        raise RuntimeError('Downloaded lunar atlas failed the pinned Git blob check')
    Atlas.parent.mkdir(parents=True, exist_ok=True)
    Atlas.write_bytes(Contents)


if __name__ == '__main__':
    subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=Vehicle, check=True)
    RestoreAtlas()
    print('Vehicle reference dependencies restored.')
