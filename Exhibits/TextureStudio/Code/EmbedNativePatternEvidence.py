#============================================================================================================================================
#                                                    EMBEDNATIVEPATTERNEVIDENCE.PY
#============================================================================================================================================
# 🧩 Embed captured native Pattern motion in the existing self-contained UI review, preserving its earlier evidence and template.

import base64
import hashlib
from html import escape
import json
from pathlib import Path
import re

Root = Path(__file__).resolve().parents[3]
Exhibit = Root / 'Exhibits/TextureStudio'
Caption = 'NativePatternClipboard'
Motion = json.loads((Exhibit / 'Results' / (Caption + 'MotionChecks.json')).read_text())
Normal = json.loads((Exhibit / 'Results/NativePatternWorkspaceChecks.json').read_text())
Sanitized = json.loads((Exhibit / 'Results/NativePatternWorkspaceSanitizedChecks.json').read_text())
for Report in [Motion, Normal, Sanitized]:
    assert Report['result'] == 'passed'
    for Name, Digest in Report['source_sha256'].items():
        assert hashlib.sha256((Root / Name).read_bytes()).hexdigest() == Digest, 'Stale source: ' + Name
Gif = (Exhibit / 'Images' / (Caption + '.gif')).read_bytes()
assert hashlib.sha256(Gif).hexdigest() == Motion['gif_sha256']
assert Gif[:6] in [b'GIF87a', b'GIF89a']
Images = [(Caption, 'Current native Pattern: copy, paste, reorder, cut, Undo/Redo and paste in place')]
Images += [('NativePatternEndpointJoin', 'Earlier native endpoint joining — historical build')]
Images += [('NativePatternPenContinuation', 'Earlier native tangent continuation — historical build'),
           ('NativePatternJoining', 'Earlier Inspector joining — historical build')]
Figures = []
for Name, Title in Images:
    Payload = (Exhibit / 'Images' / (Name + '.gif')).read_bytes()
    Uri = 'data:image/gif;base64,' + base64.b64encode(Payload).decode()
    Figure = ('<figure><img style="display:block;width:100%;height:auto;background:#131313" '
              'src="' + Uri + '" alt="' + escape(Title, quote=True) + '">'
              '<figcaption>' + escape(Title) + '</figcaption></figure>')
    Figures.append(Figure if Name == Caption else '<details><summary>' + escape(Title) + '</summary>' + Figure + '</details>')
Begin = '<!-- NATIVE PATTERN MOTION BEGIN -->'
End = '<!-- NATIVE PATTERN MOTION END -->'
Section = '\n'.join([Begin, '<section id="PatternNativeMotion" aria-labelledby="PatternNativeMotionTitle">',
    '<h2 id="PatternNativeMotionTitle">Native Pattern · clipboard and layer order</h2>',
    '<p>Session-local Copy/Cut/Paste, paste in place, independent duplication, Select All and stable layer-stack commands. '
    'Copies retain editable geometry, Boolean expressions, masks, nested group ancestry and channel contributions.</p>',
    '<p>Actual C++ ImGui cursor/input and software-Vulkan canvas captures; HTML displays GIFs only. '
    'No pixel stream, physical Windows window or hardware-GPU execution is claimed.</p>',
    '<p>Strict / sanitizer: ' + str(Normal['checks']) + ' / ' + str(Sanitized['checks'])
    + ' native checks; ' + str(Normal['clipboard_checks']) + ' focused clipboard checks in each run. '
    + str(Motion['captured_frames']) + ' captured frames, ' + str(Motion['encoded_frames']) + ' GIF frames.</p>',
    '<p>Clipboard commands are native document edits, not operating-system clipboard access. Each paste/cut/duplicate/stack '
    'edit is undoable; copies receive fresh layer/group identities. Full Pattern parity remains open.</p>',
    *Figures, '</section>', End])
for Relative in ['Exhibits/SpaceFormats/index.html', 'Exhibits/SpaceFormats/Code/ReviewTemplate.html']:
    File = Root / Relative
    Text = File.read_text()
    if Begin in Text:
        assert Text.count(Begin) == 1 and Text.count(End) == 1
        Text = re.sub(re.escape(Begin) + r'.*?' + re.escape(End), lambda Match: Section, Text, flags=re.S)
    else:
        Anchor = '<section id="evidence" role="tabpanel" aria-labelledby="Tab-evidence">'
        assert Text.count(Anchor) == 1
        Text = Text.replace(Anchor, Anchor + '\n' + Section, 1)
    File.write_text(Text)
Report = {'result': 'passed', 'viewer': 'Exhibits/SpaceFormats/index.html',
          'viewer_sha256': hashlib.sha256((Root / 'Exhibits/SpaceFormats/index.html').read_bytes()).hexdigest(),
          'gif_sha256': Motion['gif_sha256'], 'current_caption': Caption,
          'historical_captions': [Name for Name, Title in Images[1:]],
          'original_gallery_preserved': True, 'pixel_stream': False,
          'source_sha256': {'Exhibits/TextureStudio/Code/EmbedNativePatternEvidence.py':
                           hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}}
(Exhibit / 'Results/NativePatternEvidencePageChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
print('PASS: current native GIF and labelled historical GIFs embedded in the existing review and its template.')
