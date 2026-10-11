#!/usr/bin/env python3
"""Build and run the reproducible gas reading checks, then record the executed commands and hashes.

    python3 Exhibits/Workbench/GasFluid/RunNativeGasFieldChecks.py

Dependency-free: the whole of Engine/VolumetricDynamics is header-only and pulls in nothing but WindField.h,
which is itself header-only. No ImGui, no window, no Vulkan, no bootstrap. Captures land in
Exhibits/Gallery/GasField.

The run is executed twice into separate build directories and the two transcripts are compared, because the
headline claim of CoarseGasField.h is that the reading is reproducible. A check that only ever runs once
cannot observe that.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[3]
Build = Root / '.cache/gasbuild'
Gallery = Root / 'Exhibits/Gallery/GasField'
Build.mkdir(parents=True, exist_ok=True)
Gallery.mkdir(parents=True, exist_ok=True)

Include = ['-IEngine/VolumetricDynamics', '-IEngine/PhysicalDynamics', '-IEngine/DisplayPresentation', '-IEngine/ContentInterchange']
Flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Wno-unused-parameter']
Source = 'Exhibits/Workbench/GasFluid/NativeGasFieldChecks.cpp'
Raymarch = 'Exhibits/Workbench/GasFluid/NativeGasRaymarch.cpp'
SceneCodec = 'Exhibits/Workbench/GasFluid/NativeGasSceneCodec.cpp'
SceneHost = 'Exhibits/Workbench/GasFluid/NativeGasSceneHost.cpp'
StandaloneHost = 'Projects/Project-Gas/Source/GasHostMain.cpp'
Emitters = 'Exhibits/Workbench/GasFluid/NativeGasEmitters.cpp'

# The transcription must be current before anything is compiled against it. A stale header would still
# build and still resolve presets; the only symptom would be a native plume that no longer matches the
# browser one it was tuned against, which is exactly the failure a generated file exists to prevent.
Commands, Transcripts = [], []
for Generator in ('GenerateGasPresets.py', 'GenerateGasSceneCodec.py'):
    Generate = [sys.executable, str(Root / 'Tools/Build' / Generator), '--check']
    Staleness = subprocess.run(Generate, cwd=Root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(Staleness.stdout, end='')
    assert Staleness.returncode == 0, f'{Generator} reports its output is stale against presets.js'
    Commands.append(Generate)
for Attempt in ('First', 'Second'):
    Program = Build / f'GasFieldChecks{Attempt}'
    Command = ['g++', *Flags, *Include, Source, '-o', str(Program)]
    subprocess.run(Command, cwd=Root, check=True)
    Commands.append(Command)

    Result = subprocess.run([str(Program)], cwd=Root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    Transcripts.append(Result.stdout)
    if Attempt == 'First':
        print(Result.stdout, end='')
    assert Result.returncode == 0, f'gas field checks failed on the {Attempt.lower()} run'

# The reproducibility claim, observed across two separate builds rather than merely asserted inside one.
assert Transcripts[0] == Transcripts[1], 'two runs of the gas field checks disagreed'
print('Two independent builds produced identical transcripts.')

# The volume integration, and the captures it writes. -O2 because the march is the slow part by a wide
# margin and the proof is run on every push.
Gallery.mkdir(parents=True, exist_ok=True)
Marching = Build / 'GasRaymarch'
Command = ['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', *Include,
           Raymarch, '-o', str(Marching)]
subprocess.run(Command, cwd=Root, check=True)
Commands.append(Command)

Rendered = subprocess.run([str(Marching)], cwd=Root, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Rendered.stdout, end='')
assert Rendered.returncode == 0, 'gas raymarch checks failed'
(Gallery / 'RaymarchProof.txt').write_text(Rendered.stdout)

# The scene crossing. toml++ is a proof-profile package, so Tools/Bootstrap.py has already staged it in CI;
# locally it is fetched the same way. The claim is byte equality against the committed corpus, which the
# browser tool wrote, so the two emitters cannot drift apart unnoticed.
if not (Root / 'ExternalPackages/tomlpp/include/toml++/toml.hpp').exists():
    subprocess.run([sys.executable, str(Root / 'Tools/Bootstrap.py'), '--package', 'tomlpp', '--repair'],
                   cwd=Root, check=True)

Crossing = Build / 'GasSceneCodec'
Command = ['g++', '-std=c++20', '-O1', '-Wall', '-Wextra', *Include,
           '-IExternalPackages/tomlpp/include', SceneCodec, '-o', str(Crossing)]
subprocess.run(Command, cwd=Root, check=True)
Commands.append(Command)

Crossed = subprocess.run([str(Crossing)], cwd=Root, text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Crossed.stdout, end='')
assert Crossed.returncode == 0, 'gas scene codec checks failed'
(Gallery / 'SceneCodecProof.txt').write_text(Crossed.stdout)

# The scene actually run. The codec above proves the file crosses; this proves the readings in it mean the
# same thing once they are in the solver's units, which a byte comparison cannot see. Three defects were
# found by running it that nothing else reported — see the header of NativeGasSceneHost.cpp.
Running = Build / 'GasSceneHost'
Command = ['g++', '-std=c++20', '-O1', '-Wall', '-Wextra', *Include,
           '-IExternalPackages/tomlpp/include', SceneHost, '-o', str(Running)]
subprocess.run(Command, cwd=Root, check=True)
Commands.append(Command)

Ran = subprocess.run([str(Running)], cwd=Root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(Ran.stdout, end='')
assert Ran.returncode == 0, 'gas scene host checks failed'
(Gallery / 'SceneHostProof.txt').write_text(Ran.stdout)

# The standalone host itself, built the way Projects/Project-Gas/CMakeLists.txt builds it and exercised on
# the committed corpus. Compiled here with g++ directly rather than through CMake so the proof runs on a
# machine with no CMake; the CMake path is configured in CI, where one exists.
Standalone = Build / 'Project-Gas'
Command = ['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', *Include,
           '-IExternalPackages/tomlpp/include', StandaloneHost, '-o', str(Standalone)]
subprocess.run(Command, cwd=Root, check=True)
Commands.append(Command)

HostTranscript = []
Samples = sorted((Root / 'Experimental/Fluid/Samples').glob('*.gasscene.toml'))
assert len(Samples) == 8, f'the committed corpus holds {len(Samples)} scenes, not eight'
for Sample in Samples:
    Relative = str(Sample.relative_to(Root))
    for Verb in (['read', Relative], ['cross', Relative], ['advance', Relative, '--advances', '90']):
        Spoken = subprocess.run([str(Standalone), *Verb], cwd=Root, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        HostTranscript.append('$ Project-Gas ' + ' '.join(Verb) + '\n' + Spoken.stdout)
        assert Spoken.returncode == 0, f'the standalone host failed: {Verb}'

# A refusal must still be a refusal through the host, not merely inside the codec.
Broken = Build / 'NotAScene.gasscene.toml'
Broken.write_text('format = "frontier-fluid-scene"\nversion = 99\nname = "from the future"\n')
Refused = subprocess.run([str(Standalone), 'read', str(Broken)], cwd=Root, text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
HostTranscript.append('$ Project-Gas read <a scene from a later version>\n' + Refused.stdout)
assert Refused.returncode != 0, 'the host accepted a scene it should have refused'
assert 'version' in Refused.stdout, 'the host refused without saying why'

# Three captures, each through the scene's own camera rather than a bearing chosen here. A picture taken
# through a framing the scene did not author would prove something about the framing.
for Name, Advances in (('camp_fire_steady', '110'), ('hero_detonation', '45'), ('deflector_obstacle', '120')):
    Capture = Gallery / f'Scene-{Name}.png'
    Verb = ['view', f'Experimental/Fluid/Samples/{Name}.gasscene.toml', '--picture',
            str(Capture.relative_to(Root)), '--extent', '256', '--advances', Advances]
    if Name == 'hero_detonation':
        Verb.append('--blast')
    Seen = subprocess.run([str(Standalone), *Verb], cwd=Root, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(Seen.stdout, end='')
    HostTranscript.append('$ Project-Gas ' + ' '.join(Verb) + '\n' + Seen.stdout)
    assert Seen.returncode == 0, f'the standalone host could not render {Name}'
    assert Capture.exists(), f'{Capture} was reported written and is not there'

(Gallery / 'StandaloneHostProof.txt').write_text('\n'.join(HostTranscript))
print(f'The standalone host answered {len(Samples) * 3 + 4} invocations over the committed corpus.')

# The emitter component and the two gameplay hooks. Built twice as well, because the routing layer feeds the
# solver and a determinism contract that holds inside the solver and not above it holds nowhere.
EmitterTranscripts = []
for Attempt in ('First', 'Second'):
    Program = Build / f'GasEmitters{Attempt}'
    Command = ['g++', *Flags, '-Wextra', *Include, Emitters, '-o', str(Program)]
    subprocess.run(Command, cwd=Root, check=True)
    Commands.append(Command)

    Fired = subprocess.run([str(Program)], cwd=Root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    EmitterTranscripts.append(Fired.stdout)
    if Attempt == 'First':
        print(Fired.stdout, end='')
    assert Fired.returncode == 0, f'gas emitter checks failed on the {Attempt.lower()} run'

assert EmitterTranscripts[0] == EmitterTranscripts[1], 'two runs of the gas emitter checks disagreed'
(Gallery / 'EmitterProof.txt').write_text(EmitterTranscripts[0])

(Gallery / 'Proof.txt').write_text(Transcripts[0] + 'Two independent builds produced identical transcripts.\n')

Tracked = [Root / 'Engine/VolumetricDynamics/GasSceneCodec.h',
           Root / 'Engine/VolumetricDynamics/GasSceneFields.inl',
           Root / 'Experimental/Fluid/src/SceneTomlCodec.js',
           Root / SceneCodec,
           Root / 'Engine/DisplayPresentation/VolumeRaymarch.h',
           Root / 'Engine/Shaders/GasVolumeRaymarch.slang',
           Root / Raymarch,
           Root / 'Experimental/Fluid/src/presets.js',
           Root / 'Tools/Build/GenerateGasPresets.py',
           Root / 'Engine/VolumetricDynamics/GasPresetLibrary.h',
           Root / 'Engine/VolumetricDynamics/GasQualityAllowance.h',
           Root / 'Engine/VolumetricDynamics/CoarseGasField.h',
           Root / 'Engine/VolumetricDynamics/GasCollisionIntake.h',
           Root / 'Engine/VolumetricDynamics/GasWindContribution.h',
           Root / 'Engine/VolumetricDynamics/GasEmitterComponent.h',
           Root / 'Engine/VolumetricDynamics/GasVehicleIntake.h',
           Root / 'Engine/VolumetricDynamics/GasGameplayEmission.h',
           Root / Emitters,
           Root / Source,
           Root / 'Engine/VolumetricDynamics/GasSceneResolve.h',
           Root / SceneHost,
           Root / StandaloneHost,
           Root / 'Projects/Project-Gas/CMakeLists.txt',
           Path(__file__), *sorted(Gallery.glob('*.png'))]
(Gallery / 'Commands.json').write_text(json.dumps(Commands, indent=2) + '\n')
(Gallery / 'Hashes.json').write_text(json.dumps(
    {str(Entry.relative_to(Root)): hashlib.sha256(Entry.read_bytes()).hexdigest() for Entry in Tracked},
    indent=2) + '\n')
sys.exit(0)
