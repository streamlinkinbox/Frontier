#============================================================================================================================================
#                                                   INSPECTCONFIRMATIONREFERENCES.PY
#============================================================================================================================================
# 🧩 Recover and authenticate the six user-supplied design references without changing production source or Git branches.

from pathlib import Path
import argparse
import base64
import hashlib
import json
import subprocess

RepositoryRoot = Path(__file__).resolve().parents[3]
Commit = '9bccc416609aae335e4011f0d093fa6d503141bc'
Uses = {
    'b79e438e0f6d7e1d7fb5420764363100': 'Full-screen red fatal engine error; independent of failed GPU device',
    'aa658ed8425a71143147d593b1d58c77': 'Light/dark semantic notification cards',
    '0422373405cf1817679ee4e40e6b7aa6': 'Compact notification/action strips',
    '69ca33c1f28a3f326909e3b554646a21': 'Notification cards with right-side actions',
    '71a8613a6ecb22ac20c63711c357eba9': 'Rounded confirmation dialog',
    '763e050824fe11d41d0d9c5b6c468d31': 'Colour-header semantic dialog with detail text and footer actions',
}


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--images', type=Path, default=RepositoryRoot / '_ClaudeScratch/tmp/confirmations')
    Arguments = Parser.parse_args()
    Arguments.images.mkdir(parents=True, exist_ok=True)
    Sources = []
    for Name, Use in Uses.items():
        Relative = 'SourceImages/Confirmations/' + Name + '.jpg'
        Descriptor = json.loads(subprocess.check_output([
            'gh', 'api', f'repos/SultanAladin/Frontier/contents/{Relative}?ref={Commit}'], text=True))
        Payload = base64.b64decode(Descriptor['content'])
        Blob = hashlib.sha1(b'blob ' + str(len(Payload)).encode() + b'\0' + Payload).hexdigest()
        assert Blob == Descriptor['sha'], Relative
        Destination = Arguments.images / (Name + '.jpg')
        Destination.write_bytes(Payload)
        Dimensions = subprocess.check_output(['identify', '-format', '%wx%h', str(Destination)], text=True)
        Sources.append({'path': Relative, 'git_blob': Blob, 'sha256': hashlib.sha256(Payload).hexdigest(),
                        'dimensions': Dimensions, 'intended_use': Use,
                        'url': f'https://github.com/SultanAladin/Frontier/blob/{Commit}/{Relative}'})
    Report = {
        'result': 'passed', 'date': '2026-10-17', 'repository': 'SultanAladin/Frontier', 'commit': Commit,
        'scope': 'Source authentication and design mapping only; no new production UI implementation or runtime test',
        'sources': Sources,
        'checks': ['All six JPEGs match Git blobs in the pinned remote commit',
                   'All six JPEGs decode with ImageMagick identify; dimensions recorded'],
        'limitations': ['Images are design references, not new native rendering evidence',
                        'Artwork licensing is not established by user-supplied screenshots',
                        'The current host uses DX11; Vulkan device-loss integration is not present'],
        'reproduce': 'python Exhibits/SpaceFormats/Code/InspectConfirmationReferences.py',
    }
    (RepositoryRoot / 'Exhibits/SpaceFormats/Results/ConfirmationReferences.json').write_text(
        json.dumps(Report, indent=2) + '\n')
    print('PASS: six uploaded JPEG references authenticated and decoded; design inspection only')


if __name__ == '__main__':
    Main()
