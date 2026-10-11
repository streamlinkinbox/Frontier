#!/usr/bin/env python3
"""Run the native proof with runtime assets in the build tree; never modify tracked captures."""
from pathlib import Path
import shutil, subprocess, sys
root = Path(__file__).resolve().parents[2]
program, work = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
work.mkdir(parents=True, exist_ok=True)
shutil.copytree(root/'EngineContent', work/'EngineContent', dirs_exist_ok=True)
(work/'Exhibits/Gallery/NativeBillboards').mkdir(parents=True, exist_ok=True)
result = subprocess.run([str(program)], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(work/'NativeProof.txt').write_text(result.stdout)
print(result.stdout)
sys.exit(result.returncode)
