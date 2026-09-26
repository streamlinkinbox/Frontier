#!/usr/bin/env python3
"""One entry point for the consolidated checkout. Requires Python 3.11+, Git; verification also needs CMake/Ninja/C++20."""
from pathlib import Path
import argparse, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--all-dependencies',action='store_true',help='Also install application, physics and import-library dependencies')
p.add_argument('--verify',action='store_true',help='Build and run the actual native editor/CPU scene tests')
p.add_argument('--browser',action='store_true',help='Install and build the separate browser experiment (requires Node.js/npm)')
p.add_argument('--offline',action='store_true',help='Use installed sources/cached archives only (not compatible with --browser)')
p.add_argument('--cache-dir',help='Dependency archive cache directory')
a=p.parse_args()
if a.offline and a.browser:p.error('--offline cannot be combined with --browser (npm may download)')
def run(command):subprocess.run(command,cwd=ROOT,check=True)
run([sys.executable,'Tools/Bootstrap.py','--profile','all' if a.all_dependencies else 'proof'] + (['--offline'] if a.offline else []) + (['--cache-dir',a.cache_dir] if a.cache_dir else []))
if a.verify:
    run(['cmake','--preset','native-proof']);run(['cmake','--build','--preset','native-proof']);run(['ctest','--preset','native-proof'])
if a.browser:
    npm='npm.cmd' if sys.platform=='win32' else 'npm'
    run([npm,'--prefix','Experimental/FrontierEditor','ci'])
    run([npm,'--prefix','Experimental/FrontierEditor','run','build'])
print('Setup complete. Read README.md for the native application and browser run commands.')
