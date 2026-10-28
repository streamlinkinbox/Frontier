#============================================================================================================================================
#                                                        PREPAREREPLAYTOOLS.PY
#============================================================================================================================================
# 🧩 Recover checksummed Linux host-verification Slang and SwiftShader tools without installing torch or Chromium.

from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[1]
ToolDirectory = RepositoryRoot / 'ExternalPackages/VulkanCompute'


def RetrieveArchive(Url, Digest, Algorithm='sha256'):
    with urllib.request.urlopen(Url, timeout=120) as Response:
        ArchiveBytes = Response.read()
    Actual = hashlib.new(Algorithm, ArchiveBytes).digest()
    Expected = base64.b64decode(Digest) if Algorithm == 'sha512' else bytes.fromhex(Digest)
    if Actual != Expected:
        raise RuntimeError('Archive checksum mismatch: ' + Url)
    return ArchiveBytes


def VerifyFiles(Manifest):
    for Relative, Expected in Manifest['files'].items():
        Filename = ToolDirectory / Relative
        if not Filename.is_file() or hashlib.sha256(Filename.read_bytes()).hexdigest() != Expected:
            raise RuntimeError('Missing or mismatched replay tool: ' + str(Filename))
    print('Verified pinned Slang 2026.14.1 and SwiftShader host files')


def Main():
    Arguments = argparse.ArgumentParser(description='Linux x86-64 verification tools, not Windows deliverables')
    Arguments.add_argument('--verify', action='store_true', help='Only check local files; never download')
    Options = Arguments.parse_args()
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'AMD64'):
        raise RuntimeError('These pins are Linux x86-64 host-verification tools only')
    Manifest = json.loads((RepositoryRoot / 'Automation/ReplayToolDependencies.json').read_text())
    if Options.verify:
        VerifyFiles(Manifest)
        return 0
    try:
        VerifyFiles(Manifest)
        return 0
    except RuntimeError:
        pass
    if not shutil.which('node'):
        raise RuntimeError('Node.js is required only to decompress the pinned Brotli runtime archive')
    # 📝 Refuse to overwrite unmanaged/mismatched directories. Preserve them for diagnosis.
    for Name in ('Slang', 'SwiftShader'):
        if (ToolDirectory / Name).exists():
            raise RuntimeError('Preserve the existing mismatched directory before recovery: ' + Name)
    Compiler = RetrieveArchive(Manifest['compiler']['url'], Manifest['compiler']['sha256'])
    Package = RetrieveArchive(Manifest['runtime']['url'], Manifest['runtime']['sha512'], 'sha512')
    with tarfile.open(fileobj=io.BytesIO(Package), mode='r:gz') as Archive:
        Member = Archive.getmember('package/bin/swiftshader.tar.br')
        if not Member.isfile():
            raise RuntimeError('SwiftShader payload must be a regular file')
        Compressed = Archive.extractfile(Member).read()
    if hashlib.sha256(Compressed).hexdigest() != Manifest['runtime']['payload_sha256']:
        raise RuntimeError('SwiftShader payload checksum mismatch')
    Unpacked = subprocess.run(['node', '-e',
        'const fs=require("node:fs"),z=require("node:zlib");'
        'process.stdout.write(z.brotliDecompressSync(fs.readFileSync(0)));'],
        input=Compressed, stdout=subprocess.PIPE, check=True).stdout
    ToolDirectory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(Compiler)) as Archive:
        for Member in Archive.infolist():
            Relative = Path(Member.filename)
            Destination = ToolDirectory / 'Slang' / Relative
            if Relative.is_absolute() or '..' in Relative.parts:
                raise RuntimeError('Unsafe compiler archive path')
            if Member.is_dir():
                continue
            Destination.parent.mkdir(parents=True, exist_ok=True)
            Destination.write_bytes(Archive.read(Member))
        (ToolDirectory / 'Slang/slangtorch/bin/slangc').chmod(0o755)
    Destination = ToolDirectory / 'SwiftShader'
    Destination.mkdir()
    with tarfile.open(fileobj=io.BytesIO(Unpacked)) as Archive:
        for Member in Archive.getmembers():
            if not Member.isfile() or Path(Member.name).name != Member.name or Member.name in ('.', '..'):
                raise RuntimeError('Unsafe runtime archive member')
            (Destination / Member.name).write_bytes(Archive.extractfile(Member).read())
    # 📝 Only the Vulkan loader/ICD are used. EGL/GLES payloads are retained as supplied but never loaded here.
    VerifyFiles(Manifest)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as Error:
        print('Replay tool recovery failed:', Error, file=sys.stderr)
        sys.exit(1)
