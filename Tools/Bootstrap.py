#!/usr/bin/env python3
"""Materialize locked public third-party dependencies. Never fetch Frontier source or apply engine overlays."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, os, shutil, subprocess, sys, tarfile, tempfile, urllib.request, ssl, time, http.client
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

class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.lower().startswith('https://'):
            raise RuntimeError('Refusing a non-HTTPS dependency redirect')
        return super().redirect_request(req, fp, code, msg, headers, newurl)

def open_https(url, context):
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context), HTTPSRedirect())
    return opener.open(url, timeout=180)

def download(package, target, downloader='auto', ca_file=None, attempts=3):
    """HTTPS only, bounded retries; curl uses platform trust, NEVER --insecure."""
    if not package['url'].startswith('https://'):
        raise RuntimeError('Dependency downloads must use HTTPS')
    methods = ['urllib', 'curl'] if downloader == 'auto' else [downloader]
    errors = []
    for method in methods:
        if method == 'curl' and not shutil.which('curl'):
            errors.append('curl not found on PATH'); continue
        for attempt in range(attempts):
            try:
                target.unlink(missing_ok=True)
                print(f"Download: {package['name']} ({method}, attempt {attempt+1}/{attempts})", flush=True)
                if method == 'urllib':
                    context = ssl.create_default_context(cafile=ca_file)
                    with open_https(package['url'], context) as incoming, target.open('wb') as outgoing:
                        shutil.copyfileobj(incoming, outgoing)
                else:
                    command = ['curl', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https',
                               '--connect-timeout', '30', '--max-time', '900', '--output', str(target)]
                    if ca_file: command += ['--cacert', ca_file]
                    subprocess.run(command + [package['url']], check=True)
                if digest(target) != package['sha256']:
                    raise RuntimeError('Archive checksum mismatch: ' + package['name'])
                return
            except RuntimeError:
                target.unlink(missing_ok=True)
                raise
            except (OSError, http.client.HTTPException, subprocess.CalledProcessError) as error:
                errors.append(f'{method}: {error}')
                target.unlink(missing_ok=True)
                if attempt+1 < attempts: time.sleep(min(attempt+1, 2))
    raise RuntimeError('Download failed; existing installations were not changed. '
                       'See Docs/Building.md (proxy/CA and offline archive recovery). ' + '; '.join(errors))

def install(package, root=ROOT, check=False, offline=False, repair=False, cache_dir=None,
            downloader='auto', ca_file=None):
    name = package['name']; destination = root/'ExternalPackages'/name
    marker = destination/'.frontier-dependency.json'
    identity = {key: package[key] for key in ('revision', 'sha256')}
    installed = None
    if marker.exists():
        try: installed = json.loads(marker.read_text())
        except (ValueError, OSError): pass
    ready = installed == identity and (destination/package['witness']).is_file()
    if ready and not repair:
        print('Ready:', name); return
    remedy = f'python Tools/Bootstrap.py --package {name} --repair'
    if check:
        if not destination.exists(): reason = 'not installed'
        elif not marker.exists(): reason = 'unmanaged folder (identity marker missing)'
        elif not isinstance(installed, dict): reason = 'invalid identity marker'
        elif installed != identity: reason = f"wrong pin (expected {identity['revision']}, found {installed.get('revision', 'unknown')}; archive identity must also match)"
        else: reason = f"incomplete installation (missing {package['witness']})"
        raise RuntimeError(f'{name}: {reason} at {destination}. '
                           f'Install with --package {name}; for a managed installation use: {remedy}. No download attempted.')
    occupied = destination.exists() and any(destination.iterdir())
    if occupied and not (repair and marker.exists()):
        raise RuntimeError(f'{destination} contains an unmanaged or different/incomplete version. '
                           f'For managed packages use: {remedy}. Otherwise move this directory aside first. Nothing overwritten.')
    cache = Path(cache_dir) if cache_dir else root/'.cache/dependency-archives'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache/f"{name}-{package['revision']}.tar.gz"
    if not archive.exists():
        if offline:
            raise RuntimeError(f'{name}: offline cache miss: {archive}. Copy the locked archive here; no network attempted.')
        with tempfile.TemporaryDirectory(prefix=name+'-', dir=cache) as temporary:
            partial = Path(temporary)/'archive.partial'
            download(package, partial, downloader, ca_file)
            partial.replace(archive)
    if digest(archive) != package['sha256']:
        raise RuntimeError(f'Cached archive checksum mismatch: {archive}; quarantine ONLY this archive and retry. Installed sources unchanged.')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=name+'-', dir=destination.parent) as temporary:
        staged = Path(temporary)/'package'; staged.mkdir()
        extract(archive, staged, package.get('files'))
        if not (staged/package['witness']).is_file():
            raise RuntimeError('Archive lacks required source: ' + name)
        (staged/'.frontier-dependency.json').write_text(json.dumps(identity, sort_keys=True)+'\n')
        (staged/'.frontier-proof-pin').write_text(package['revision']+'\n')
        backup = None
        if occupied:
            backup_root = destination.parent/'.frontier-backups'; backup_root.mkdir(exist_ok=True)
            backup = backup_root/f'{name}-{time.time_ns()}'
            destination.rename(backup)
        elif destination.exists(): destination.rmdir()
        try: staged.rename(destination)
        except OSError:
            if backup: backup.rename(destination)
            raise
        if backup: print('Previous installation preserved:', backup)
    print('Installed:', name)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['all', 'proof'], default='all')
    parser.add_argument('--check', action='store_true', help='Check installed dependency identities without network access')
    parser.add_argument('--offline', action='store_true', help='Install/repair from cached archives only; never download')
    parser.add_argument('--package', action='append', default=[], help='Select a locked package by name (repeatable)')
    parser.add_argument('--repair', action='store_true', help='Reinstall selected managed packages with backups; requires --package')
    parser.add_argument('--cache-dir', default=os.environ.get('FRONTIER_DEPENDENCY_CACHE'), help='Archive cache (or FRONTIER_DEPENDENCY_CACHE)')
    parser.add_argument('--downloader', choices=['auto', 'urllib', 'curl'], default='auto')
    parser.add_argument('--ca-file', help='Trusted PEM CA bundle for Python/curl HTTPS')
    args = parser.parse_args()
    if args.repair and (not args.package or args.check): parser.error('--repair requires --package and cannot be combined with --check')
    packages = json.loads((ROOT/'ExternalPackages/Dependencies.lock.json').read_text())['packages']
    unknown = set(args.package) - {p['name'] for p in packages}
    if unknown: parser.error('Unknown package(s): ' + ', '.join(sorted(unknown)))
    selected = [p for p in packages if (p['name'] in args.package if args.package else args.profile == 'all' or p['proof'])]
    errors = []
    for package in selected:
        try:
            install(package, check=args.check, offline=args.offline, repair=args.repair,
                    cache_dir=args.cache_dir, downloader=args.downloader, ca_file=args.ca_file)
        except (RuntimeError, OSError) as error:
            if not args.check: raise
            errors.append(str(error))
    if errors: raise RuntimeError('\n'.join(errors))
    if any(p['name'] == 'imgui' for p in selected):
        subprocess.run([sys.executable, str(ROOT/'Tools/Build/ApplyImGuiPatches.py'), *(['--verify'] if args.check else [])], cwd=ROOT, check=True)
    print('Frontier ready. No other Frontier repository or engine patch assembly is required.')

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print('Setup failed:', error, file=sys.stderr)
        sys.exit(1)
