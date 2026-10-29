#============================================================================================================================================
#                                                       PREPAREICONEVIDENCE.PY
#============================================================================================================================================
# 🧩 Compile the existing pinned software SVG renderer for local evidence; no packages, downloads or application delivery.

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import importlib.util
import subprocess

RepositoryRoot = Path(__file__).resolve().parents[3]


def PrepareIconEvidence(Destination):
    Destination = Path(Destination).resolve()
    Destination.mkdir(parents=True, exist_ok=True)
    Specification = importlib.util.spec_from_file_location('ThorvgStaging', RepositoryRoot / 'Automation/StageThorVG.py')
    Module = importlib.util.module_from_spec(Specification)
    Specification.loader.exec_module(Module)
    Module.stage(RepositoryRoot / 'ExternalPackages/thorvg', Destination / 'tvgInitializer.cpp')
    (Destination / 'config.h').write_text('#pragma once\n#define THORVG_VERSION_STRING "1.0.0"\n'
        '#define THORVG_SW_RASTER_SUPPORT 1\n#define THORVG_SVG_LOADER_SUPPORT 1\n'
        '#define THORVG_THREAD_SUPPORT 1\n#define THORVG_FILE_IO_SUPPORT 1\n')
    Directories = ['src/common', 'src/renderer', 'src/renderer/sw_engine', 'src/loaders/svg', 'src/loaders/raw']
    Includes = ['-IExternalPackages/thorvg/inc', '-I' + str(Destination)]
    Includes += ['-IExternalPackages/thorvg/' + Directory for Directory in Directories]
    Sources = [Destination / 'tvgInitializer.cpp']
    for Directory in Directories:
        Sources += [Source for Source in sorted((RepositoryRoot / 'ExternalPackages/thorvg' / Directory).glob('*.cpp'))
                    if Source.name != 'tvgInitializer.cpp']
    HeaderDigest = hashlib.sha256(b''.join(Source.read_bytes() for Source in sorted(
        (RepositoryRoot / 'ExternalPackages/thorvg').rglob('*.h')))).digest()

    def Compile(Source):
        Digest = hashlib.sha256(Source.read_bytes() + HeaderDigest + b'g++-O1-TVG_STATIC-TVG_BUILD').hexdigest()[:20]
        Object = Destination / (Source.stem + '-' + Digest + '.o')
        if not Object.exists():
            subprocess.run(['g++', '-std=c++20', '-O1', '-DTVG_STATIC', '-DTVG_BUILD'] + Includes +
                           ['-c', str(Source), '-o', str(Object)], cwd=RepositoryRoot, check=True)
        return Object

    with ThreadPoolExecutor(max_workers=2) as Workers:
        Objects = list(Workers.map(Compile, Sources))
    Library = Destination / 'libThorvgEvidence.a'
    subprocess.run(['ar', 'rcs', str(Library)] + [str(Object) for Object in Objects], cwd=RepositoryRoot, check=True)
    return Library


if __name__ == '__main__':
    import argparse
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--destination', type=Path, required=True)
    print(PrepareIconEvidence(Parser.parse_args().destination), flush=True)
