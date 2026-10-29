#============================================================================================================================================
#                                                      VERIFYNATIVEPATTERNSTREAM.PY
#============================================================================================================================================
# 🧩 Verify native stream transport, bounded routes and real pen-driven source revision changes through HTTP input.

import argparse
import hashlib
import json
from pathlib import Path
import urllib.error
import urllib.request

Root = Path(__file__).resolve().parents[3]
Parser = argparse.ArgumentParser()
Parser.add_argument('--url', default='http://127.0.0.1:8776')
Arguments = Parser.parse_args()
Checks = 0


def Confirm(Condition, Caption):
    global Checks
    if not Condition:
        raise RuntimeError(Caption)
    Checks += 1


def Request(Route, Payload=None, Headers=None):
    return urllib.request.urlopen(urllib.request.Request(Arguments.url + Route, data=Payload, headers=Headers or {}), timeout=60)


def Input(X=0, Y=0, Buttons=0, Key=0, Pressed=0):
    Payload = json.dumps([X, Y, Buttons, 0, 0, 0, 0, Key, Pressed, 1, 0]).encode()
    return json.loads(Request('/input', Payload, {'Content-Type': 'application/json'}).read())


def Refuse(Route, Payload, Headers, Expected):
    try:
        Request(Route, Payload, Headers)
    except urllib.error.HTTPError as Failure:
        Confirm(Failure.code == Expected, 'Incorrect stream refusal code')
    else:
        raise RuntimeError('Unbounded or cross-origin stream request accepted')


Page = Request('/').read()
Confirm(b'C++ ImGui' in Page and b'frame.png' in Page, 'Native pixel transport page unavailable')
Before = Input()
PixelsBefore = Request('/frame.png').read()
Confirm(PixelsBefore.startswith(b'\x89PNG\r\n\x1a\n'), 'Native capture is not PNG')
# 📝 Fixed 1440×900 native surface; toolbar and board are driven through their actual screen coordinates.
Input(Key=3, Pressed=1)
Input(Key=3)
for X, Y in [(590, 455), (720, 455), (675, 580)]:
    Input(X, Y)
    Input(X, Y, Buttons=1)
    Pending = Input(X, Y)
Confirm(Pending['revision'] == Before['revision'], 'Native pen preview changed source history')
Confirm(Pending['layers'] == Before['layers'] + 1, 'Native pen preview did not retain a temporary contour')
Finished = Input(Key=6, Pressed=1)
Input(Key=6)
Confirm(Finished['revision'] == Before['revision'] + 1 and Finished['layers'] == Before['layers'] + 1,
        'Native Enter did not finalize exactly one pen contour')
PixelsAfter = Request('/frame.png').read()
Confirm(PixelsAfter != PixelsBefore, 'HTTP native input did not change actual captured pixels')
for Route in ['/../.git/config', '/Engine/ContentInterchange/CubicPathEditing.cpp', '/input']:
    Refuse(Route, None, None, 404)
Refuse('/input', b'[]', {'Content-Type': 'application/json'}, 400)
Refuse('/input', b'{}', {'Content-Type': 'text/plain'}, 415)
Refuse('/input', b'[]', {'Content-Type': 'application/json', 'Origin': 'https://unrelated.invalid'}, 403)
Refuse('/input', b'[]', {'Content-Type': 'application/json', 'Sec-Fetch-Site': 'cross-site'}, 403)
Refuse('/input', json.dumps([float('inf')] * 11).encode(), {'Content-Type': 'application/json'}, 400)
Files = ['Exhibits/TextureStudio/Code/ServeNativePattern.py', 'Exhibits/TextureStudio/Code/VerifyNativePatternStream.py',
         'Exhibits/TextureStudio/NativePatternStream.html', 'Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp']
Report = {'result': 'passed', 'checks': Checks, 'transport': 'HTTP to persistent native C++ ImGui runner',
          'browser_executed': False, 'os_window_created': False, 'windows_executed': False,
          'native_before': Before, 'native_after': Finished,
          'capture_before_sha256': hashlib.sha256(PixelsBefore).hexdigest(),
          'capture_after_sha256': hashlib.sha256(PixelsAfter).hexdigest(),
          'source_sha256': {Name: hashlib.sha256((Root / Name).read_bytes()).hexdigest() for Name in Files}}
Destination = Root / 'Exhibits/TextureStudio/Results/NativePatternStreamChecks.json'
Destination.write_text(json.dumps(Report, indent=2) + '\n')
print(json.dumps({'result': 'passed', 'checks': Checks}), flush=True)
