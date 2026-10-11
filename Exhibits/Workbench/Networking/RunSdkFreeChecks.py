#!/usr/bin/env python3
"""Compile and run every Project-Networking check that needs neither the EOS SDK nor Photon.

Most of Project-Networking can be held to account on a bare machine: the wire codec, the transport
router, the lobby's local configuration and history, the login acceptance ordering, and the whole
replication runtime. Only the three translation units that include ``eos_sdk.h`` and the real Photon
client are left out, and ``RunChecks.py`` covers those where an SDK is present.

Every source that can compile without an SDK is also compiled here under ``-Wall -Wextra -Werror``,
so a warning introduced in the SDK-only build path is still caught on a runner that has no SDK.

    python3 Exhibits/Workbench/Networking/RunSdkFreeChecks.py
"""
import json
import pathlib
import subprocess
import sys

Here = pathlib.Path(__file__).resolve().parent
Root = Here.parents[2]
Source = Root / 'Projects/Project-Networking/Source'
Build = Root / '.cache/networkingbuild'

Flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', f'-I{Source}']

# Sources with no SDK header anywhere in their include graph. The rest — EpicExchange, LobbyRuntime,
#    SessionHistory, LoginHost, WindowHost, EcomOwnership, PhotonTransport — reach an SDK.
Compilable = ['BackendClient.cpp', 'EosTransport.cpp', 'TransportRouter.cpp', 'PhotonLinkStub.cpp',
              'ReplicationLink.cpp', 'PhotonReplicationLink.cpp', 'ReplicationSequence.cpp']

# Checks that are one self-contained translation unit each.
Checks = ['TransportChecks.cpp', 'LobbyChecks.cpp', 'LoginSequenceChecks.cpp']


def Run(Command, **Rest):
    print('$ ' + ' '.join(str(Word) for Word in Command), flush=True)
    return subprocess.run([str(Word) for Word in Command], check=True, **Rest)


def main() -> int:
    Build.mkdir(parents=True, exist_ok=True)
    Spoken = []

    for Name in Compilable:
        Run(['g++', *Flags, '-c', Source / Name, '-o', Build / (Name + '.o')])
    print(f'Compiled {len(Compilable)} SDK-free sources with no warnings.', flush=True)

    for Name in Checks:
        Program = Build / Name[:-4]
        Run(['g++', *Flags, Here / Name, '-o', Program])
        Said = Run([Program], stdout=subprocess.PIPE, text=True).stdout
        print(Said, end='', flush=True)
        Lines = [Line for Line in Said.splitlines() if Line.strip()]
        if any(Line.startswith('FAIL') for Line in Lines):
            print(f'{Name} reported a failure', file=sys.stderr)
            return 1
        Spoken.append({'check': Name, 'passes': sum(Line.startswith('PASS') for Line in Lines),
                       'closing': Lines[-1] if Lines else ''})

    Passes = sum(Entry['passes'] for Entry in Spoken)
    print(f'PASS {Passes} checks across {len(Checks)} SDK-free networking proofs, and '
          f'{len(Compilable)} sources compiled warning-free.', flush=True)
    (Here / 'SdkFreeEvidence.json').write_text(
        json.dumps({'compiled': Compilable, 'checks': Spoken, 'passes': Passes}, indent=2) + '\n',
        encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
