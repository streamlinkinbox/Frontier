#!/usr/bin/env python3
"""Build and run the replication proof, then record the executed commands and hashes.

    python3 Exhibits/Workbench/Networking/RunReplicationChecks.py

No SDK, no socket, no window: the proof runs the whole replication sequence over the in-process
loopback carrier, and asks the Photon, Epic and PlayFab carriers to answer for themselves. Photon is
linked through PhotonLinkStub.cpp here, which is what reports it unavailable; on Windows the real
PhotonTransport.cpp takes its place and nothing above it changes.

A second pass runs under AddressSanitizer and UndefinedBehaviorSanitizer, because half of what the
proof asserts is that a hostile snapshot is refused rather than read past, and a plain build cannot
tell the difference between refusing and getting away with it.
"""
import hashlib
import json
import pathlib
import subprocess
import sys

Root = pathlib.Path(__file__).resolve().parents[3]
Build = Root / '.cache/replicationbuild'
Evidence = pathlib.Path(__file__).resolve().parent
Build.mkdir(parents=True, exist_ok=True)

Source = Root / 'Projects/Project-Networking/Source'
Include = ['-I' + str(Source)]
Flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror']

Sources = [Source / 'ReplicationSequence.cpp',
           Source / 'ReplicationLink.cpp',
           Source / 'PhotonReplicationLink.cpp',
           Source / 'PhotonLinkStub.cpp',
           Evidence / 'ReplicationChecks.cpp']

Commands, Report = [], []


def Run(Command, Label):
    Commands.append([str(x) for x in Command])
    Result = subprocess.run([str(x) for x in Command], cwd=Root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    Report.append(f'--- {Label}\n{Result.stdout.rstrip()}\nexit={Result.returncode}')
    if Result.returncode != 0:
        print('\n'.join(Report))
        raise SystemExit(f'{Label} failed with exit {Result.returncode}')
    return Result.stdout


Plain = Build / 'ReplicationProof'
Run(['g++', *Flags, *Include, *Sources, '-o', Plain], 'build')
Spoken = Run([Plain], 'run')
print(Spoken, end='')

Sanitized = Build / 'ReplicationProofSanitized'
Run(['g++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
     *Include, *Sources, '-o', Sanitized], 'build (sanitized)')
Run([Sanitized], 'run (sanitized)')
print('PASS sanitized: no out-of-bounds read, no undefined behaviour.')

if 'PASS' not in Spoken:
    raise SystemExit('the replication proof did not report PASS')

Tracked = [Source / 'ReplicationLink.h', Source / 'ReplicationLink.cpp',
           Source / 'ReplicatedPlacement.h', Source / 'ReplicationSequence.h',
           Source / 'ReplicationSequence.cpp', Source / 'PhotonReplicationLink.cpp',
           Source / 'PhotonTransport.h', Evidence / 'ReplicationChecks.cpp',
           pathlib.Path(__file__)]
(Evidence / 'ReplicationChecks.log').write_text('\n'.join(Report) + '\n')
(Evidence / 'ReplicationEvidence.json').write_text(json.dumps({
    'commands': Commands,
    'spoken': Spoken.strip(),
    'sources': {str(Item.relative_to(Root)): hashlib.sha256(Item.read_bytes()).hexdigest()
                for Item in Tracked},
}, indent=2) + '\n')
sys.exit(0)
