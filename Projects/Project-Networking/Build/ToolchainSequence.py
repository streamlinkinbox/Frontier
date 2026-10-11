"""Build the real EOS Linux diagnostic and project code image; never download dependencies."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', required=True, type=pathlib.Path)
    parser.add_argument('--slate-root', type=pathlib.Path,
                        help='Other checkout carrying Frontier/Engine/ProjectInterchange; this '
                             'repository supplies Engine/ProjectInterchange by itself')
    parser.add_argument('--gui-root', type=pathlib.Path, help='Optional folder with pinned glfw and imgui sources')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('Windows builds use ToolchainSequence.ps1 (MSVC /MD).')
    project = pathlib.Path(__file__).resolve().parent.parent
    sdk = args.sdk_root.resolve()
    interchange = project.parents[2] / 'Engine/ProjectInterchange'
    if args.slate_root is not None:
        donor = args.slate_root.resolve() / 'Frontier/Engine/ProjectInterchange'
        if (donor / 'ProjectInterchange.h').is_file():
            interchange = donor
    runtime = sdk / 'Bin/libEOSSDK-Linux-Shipping.so'
    for required in (runtime, sdk / 'Include/eos_sdk.h', interchange / 'ProjectInterchange.h'):
        if not required.is_file():
            parser.error(f'Missing dependency: {required}')
    output = project / 'Build/Output'
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(runtime, output / runtime.name)
    flags = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread',
             '-I' + str(sdk / 'Include'), '-I' + str(interchange)]
    link = ['-L' + str(output), '-lEOSSDK-Linux-Shipping', '-Wl,-rpath,$ORIGIN', '-Wl,-z,defs']
    source = project / 'Source'
    commands = [
        flags + [str(source / 'EpicExchange.cpp'), str(source / 'LobbyRuntime.cpp'), str(source / 'SessionHistory.cpp'), str(source / 'LoginHost.cpp')]
        + link + ['-o', str(output / 'LoginHost')],
        flags + ['-fPIC', '-shared', str(source / 'EpicExchange.cpp'), str(source / 'LobbyRuntime.cpp'), str(source / 'SessionHistory.cpp'), str(source / 'NetworkingInterchange.cpp')]
        + link + ['-o', str(output / 'ProjectNetworking.so')],
    ]
    # The replication sequence needs neither EOS nor Photon to compile, and its proof is what the
    #    workflow runs on Linux. The Photon carrier links against the stub here.
    replication = [str(source / name) for name in
                   ('ReplicationSequence.cpp', 'ReplicationLink.cpp', 'PhotonReplicationLink.cpp',
                    'PhotonLinkStub.cpp')]
    commands.append(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I' + str(source)]
                    + replication
                    + [str(project.parents[2] / 'Exhibits/Workbench/Networking/ReplicationChecks.cpp'),
                       '-o', str(output / 'ReplicationChecks')])
    for command in commands:
        subprocess.run(command, check=True)
    if args.gui_root:
        gui_build = output / 'WindowBuild'
        subprocess.run(['cmake', '-S', str(project / 'Build/WindowHost'), '-B', str(gui_build),
                        '-DCMAKE_BUILD_TYPE=Release', '-DEOS_SDK_ROOT=' + str(sdk),
                        '-DGUI_ROOT=' + str(args.gui_root.resolve())], check=True)
        subprocess.run(['cmake', '--build', str(gui_build), '--parallel', '4'], check=True)
    evidence = {
        'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
        'sdk_runtime_sha256': digest(runtime),
        'interchange_header_sha256': digest(interchange / 'ProjectInterchange.h'),
        'sources': {p.name: digest(p) for p in sorted(source.iterdir()) if p.is_file()},
        'artifacts': {name: digest(output / name)
                      for name in ('LoginHost', 'ProjectNetworking.so', 'ReplicationChecks')},
        'authentication': 'NOT_ATTEMPTED',
    }
    (output / 'LinuxBuildEvidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('Built real EOS-linked LoginHost and ProjectNetworking.so. Authentication NOT attempted.')


if __name__ == '__main__':
    main()
