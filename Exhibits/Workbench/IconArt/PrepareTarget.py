#!/usr/bin/env python3
"""Acquire immutable CPU-proof inputs without changing any git branch."""
import argparse, json, os, subprocess, tarfile
from pathlib import Path
PIN = 'f6c99702c769ef9fd44c39ac8819e0ebeae35b9b'
PACKAGES = [
 ('imgui', 'ocornut/imgui', '3bae66c735670619baf51391eba7f3d90a25d125'),
 ('tomlpp', 'marzer/tomlplusplus', '1e8829b793b66ad17011732a146b8077d379b011'),
 ('stb', 'nothings/stb', '2c980bb59875b0d32144a71867fbdebb2f77cd20'),
 ('thorvg', 'thorvg/thorvg', '6715f99ac106b6d4587f384a78c59fe957cfd2a3'),
]
ROOT = Path(__file__).resolve().parents[3]
def acquire(repo, pin, destination):
    marker = destination / '.frontier-proof-pin'
    if marker.exists() and marker.read_text().strip() == pin: return
    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError(f'{destination} is not empty and has no matching pin. Use a fresh destination.')
    destination.mkdir(parents=True, exist_ok=True)
    archive = ROOT / '.cache' / (repo.replace('/', '-') + '-' + pin + '.tar.gz')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        with temporary.open('wb') as out:
            subprocess.run(['gh', 'api', f'repos/{repo}/tarball/{pin}'], stdout=out, check=True)
        temporary.rename(archive)
    with tarfile.open(archive) as tar:
        # GitHub archives have one top-level prefix; use Python's safe extraction.
        for member in tar.getmembers():
            pieces = member.name.split('/', 1)
            if len(pieces) < 2 or not pieces[1]: continue
            member.name = pieces[1]
            if hasattr(tarfile, 'data_filter'):
                tar.extract(member, destination, filter='data')
            else:
                # Older Python 3.11 distributions lack extraction filters. Fail closed
                # on links/special files, and validate every destination before writing.
                resolved = (destination / member.name).resolve()
                if not resolved.is_relative_to(destination.resolve()) or not (member.isfile() or member.isdir()):
                    raise RuntimeError('Unsafe or unsupported archive member: ' + member.name)
                tar.extract(member, destination)
    marker.write_text(pin + '\n')
def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--target', type=Path, default=ROOT/'.cache/cpp-target')
    args=parser.parse_args(); target=args.target.resolve()
    acquire('SultanAladin/Frontier-', PIN, target)
    for name, repo, pin in PACKAGES: acquire(repo, pin, target/'ExternalPackages'/name)
    print(json.dumps({'target':str(target),'revision':PIN,'dependencies':PACKAGES},indent=2))
    print('For archive-based proofs set GIT_CEILING_DIRECTORIES=' + str(target))
if __name__ == '__main__': main()
