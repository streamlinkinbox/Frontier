#!/usr/bin/env python3
"""Combine generated, source-positioned surface documents without transforms, caps or duplicate resets."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def Assemble(Parts, Destination):
    Lines = ['# SolidArc native document v1', '# Liger main body plus interior/windshield section, in shared source coordinates.',
             '# Main-body junction sheets are deliberately retained; not a manufacturing solid.', 'reset']
    Names, Sources = set(), []
    for Part in Parts:
        Content = Part.read_text()
        if not Content.startswith('# SolidArc native document v1'):
            raise ValueError(f'Not a native surface document: {Part}')
        for Line in Content.splitlines():
            Line = Line.strip()
            if not Line or Line.startswith('#') or Line == 'reset':
                continue
            if Line.split()[0] not in ('require', 'patch', 'sew'):
                raise ValueError(f'Only source-positioned constructive surface commands are accepted: {Part}')
            Match = re.search(r'--name=([^\s]+)', Line)
            if Match:
                if Match[1] in Names:
                    raise ValueError(f'Duplicate figure name: {Match[1]}')
                Names.add(Match[1])
            Lines.append(Line)
        Sources.append({'file': Part.name, 'sha256': hashlib.sha256(Part.read_bytes()).hexdigest()})
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Destination.write_text('\n'.join(Lines)+'\n')
    Destination.with_suffix('.assembly.json').write_text(json.dumps({
        'parts': Sources, 'documentSha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
        'placement': 'Original shared source coordinates; no transformations or inferred thickness',
        'terminology': 'The user identifies the previous cowl/frame section as interior and windshield; source filenames retained for traceability',
        'limitations': ['Main-body junction sheets unresolved', 'Dense starting surface layout', 'No G1/G2 certification']}, indent=2)+'\n')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('destination', type=Path)
    Parser.add_argument('parts', type=Path, nargs='+')
    Arguments = Parser.parse_args()
    Assemble(Arguments.parts, Arguments.destination)
