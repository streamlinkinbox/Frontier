"""Fetch only hash-pinned EOS inputs from the user's already-shared Drive files."""
import argparse
import hashlib
import json
import pathlib
import time

import gdown


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path, default=pathlib.Path(__file__).with_name("WindowsSdkManifest.json"))
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    for relative, entry in manifest['files'].items():
        destination = args.destination / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        for attempt in range(3):
            try:
                if not destination.exists():
                    downloaded = gdown.download(id=entry['drive_id'], output=str(destination), quiet=True)
                    if not downloaded:
                        raise RuntimeError('Download unavailable; check existing Drive link access')
                contents = destination.read_bytes()
                if len(contents) != entry['bytes'] or hashlib.sha256(contents).hexdigest() != entry['sha256']:
                    raise RuntimeError('Size or SHA-256 mismatch: ' + relative)
                print('Verified ' + relative, flush=True)
                break
            except Exception:
                destination.unlink(missing_ok=True)
                if attempt == 2:
                    raise
                time.sleep(3)


if __name__ == '__main__':
    main()
