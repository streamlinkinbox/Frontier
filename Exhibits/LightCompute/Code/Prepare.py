"""Restore pinned Linux x86-64 tools in ignored storage. No GitHub Actions or system installation."""
from pathlib import Path
import hashlib
import io
import json
import os
import subprocess
import tarfile
import urllib.request
import zipfile
Root = Path(__file__).resolve().parents[3]
Output = Root / 'ExternalPackages/VulkanCompute'
Output.mkdir(parents=True, exist_ok=True)
Downloads = [
    ('Slang', 'https://files.pythonhosted.org/packages/59/db/f58a546564b8992e8e4c66b3e1b99432a87acec4317b10b1b9c7927868b9/slangtorch-1.3.22-py3-none-manylinux_2_27_x86_64.whl',
     '7755f2f53509ebbd65839dcd8a104e7105aeaf3e3600aea9385c2bec9beb2eaa'),
    ('SwiftShader', 'https://registry.npmjs.org/@sparticuz/chromium/-/chromium-153.0.0.tgz',
     '20e5656fcdcf4118ede85edfb9d6bb9cc1bbb7ffb8b75622fffbcf36a835c3c0'),
    ('Headers', 'https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.321',
     '7c1874f9186d9e7d5245e279cdccaa77a66ed8f52871b428630aeb771dc7623c'),
]
for Category, Url, Digest in Downloads:
    Archive = Output / (Category + '.download')
    if not Archive.exists():
        with urllib.request.urlopen(Url, timeout=60) as Response:
            Archive.write_bytes(Response.read())
    Payload = Archive.read_bytes()
    assert hashlib.sha256(Payload).hexdigest() == Digest, 'Dependency checksum mismatch: ' + Category
    if Category == 'Slang':
        with zipfile.ZipFile(io.BytesIO(Payload)) as Package:
            for Name in Package.namelist():
                if Name.startswith('slangtorch/bin/') or 'LICENSE' in Name.upper():
                    Destination = (Output / 'Slang' / Name).resolve()
                    assert Destination.is_relative_to((Output / 'Slang').resolve())
                    if not Name.endswith('/'):
                        Destination.parent.mkdir(parents=True, exist_ok=True)
                        Destination.write_bytes(Package.read(Name))
        (Output / 'Slang/slangtorch/bin/slangc').chmod(0o755)
    elif Category == 'SwiftShader':
        with tarfile.open(fileobj=io.BytesIO(Payload)) as Package:
            Compressed = Package.extractfile('package/bin/swiftshader.tar.br').read()
        (Output / 'swiftshader.tar.br').write_bytes(Compressed)
        subprocess.run(['node', '-e',
            'const f=require("fs"),z=require("zlib");f.writeFileSync(process.argv[2],z.brotliDecompressSync(f.readFileSync(process.argv[1])));',
            str(Output / 'swiftshader.tar.br'), str(Output / 'swiftshader.tar')], check=True)
        with tarfile.open(Output / 'swiftshader.tar') as Package:
            for Name in ['libvulkan.so.1', 'libvk_swiftshader.so', 'vk_swiftshader_icd.json']:
                Destination = Output / 'SwiftShader' / Name
                Destination.parent.mkdir(parents=True, exist_ok=True)
                Destination.write_bytes(Package.extractfile(Name).read())
    else:
        with tarfile.open(fileobj=io.BytesIO(Payload)) as Package:
            for Member in Package:
                Destination = (Output / Member.name).resolve()
                assert Destination.is_relative_to(Output.resolve())
                if Member.isfile():
                    Destination.parent.mkdir(parents=True, exist_ok=True)
                    Destination.write_bytes(Package.extractfile(Member).read())
    print('Verified', Category, Digest)
(Output / 'Dependencies.json').write_text(json.dumps(Downloads, indent=2) + '\n')
