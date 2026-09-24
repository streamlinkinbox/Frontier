#!/usr/bin/env python3
"""Materialize locked public third-party dependencies. Never fetch Frontier source or apply engine overlays."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, os, shutil, subprocess, sys, tarfile, tempfile, urllib.request
ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def extract(archive, destination, only=None):
    """Strip GitHub's one root prefix. Refuse traversal, links and special files."""
    with tarfile.open(archive, 'r:gz') as source:
        for member in source:
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2:
                continue
            relative = PurePosixPath(*parts[1:])
            if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative):
                raise RuntimeError('Unsafe archive path: ' + member.name)
            if not member.isfile() and not member.isdir():
                raise RuntimeError('Unsupported archive member: ' + member.name)
            if only and str(relative) not in only:
                continue
            target = destination.joinpath(*relative.parts)
            if not target.resolve().is_relative_to(destination.resolve()):
                raise RuntimeError('Archive path escapes destination')
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as incoming, target.open('wb') as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
                target.chmod(member.mode & 0o777)

def install(package, root=ROOT, check=False):
    name = package['name']; destination = root/'ExternalPackages'/name
    marker = destination/'.frontier-dependency.json'
    identity = {key: package[key] for key in ('revision', 'sha256')}
    if marker.exists() and json.loads(marker.read_text()) == identity:
        if not (destination/package['witness']).is_file():
            raise RuntimeError(f'{name}: incomplete installation; move the directory aside and rerun setup')
        print('Ready:', name)
        return
    if check:
        raise RuntimeError(f'{name}: missing or wrong version; run python3 Tools/Bootstrap.py')
    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError(f'{destination} contains an unmanaged or different version. Move it aside; setup will not overwrite it.')
    cache = root/'.cache/dependency-archives'; cache.mkdir(parents=True, exist_ok=True)
    archive = cache/f"{name}-{package['revision']}.tar.gz"
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        try:
            print('Download:', name, flush=True)
            with urllib.request.urlopen(package['url'], timeout=180) as incoming, temporary.open('wb') as outgoing:
                shutil.copyfileobj(incoming, outgoing)
            if digest(temporary) != package['sha256']:
                raise RuntimeError('Archive checksum mismatch: ' + name)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if digest(archive) != package['sha256']:
        raise RuntimeError(f'Cached archive checksum mismatch: {archive}; remove it and retry')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=name+'-', dir=destination.parent) as temporary:
        staged = Path(temporary)/'package'; staged.mkdir()
        extract(archive, staged, package.get('files'))
        if not (staged/package['witness']).is_file():
            raise RuntimeError('Archive lacks required source: ' + name)
        (staged/'.frontier-dependency.json').write_text(json.dumps(identity, sort_keys=True)+'\n')
        # Compatibility with retained historical proof tooling; no nested git repository is created.
        (staged/'.frontier-proof-pin').write_text(package['revision']+'\n')
        if destination.exists():
            destination.rmdir()  # only a pre-existing EMPTY destination can reach here
        staged.rename(destination)
    print('Installed:', name)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['all', 'proof'], default='all')
    parser.add_argument('--check', action='store_true', help='Check installed dependency identities without network access')
    args = parser.parse_args()
    packages = json.loads((ROOT/'ExternalPackages/Dependencies.lock.json').read_text())['packages']
    for package in packages:
        if args.profile == 'all' or package['proof']:
            install(package, check=args.check)
    # Only third-party ImGui divergence remains a patch: all Frontier changes are ordinary committed source.
    subprocess.run([sys.executable, str(ROOT/'Tools/Build/ApplyImGuiPatches.py'), *(['--verify'] if args.check else [])], cwd=ROOT, check=True)
    print('Frontier ready. No other Frontier repository or engine patch assembly is required.')

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print('Setup failed:', error, file=sys.stderr)
        sys.exit(1)
