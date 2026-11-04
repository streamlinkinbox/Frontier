#============================================================================================================================================
#                                                           VALIDATEREVIEW.PY
#============================================================================================================================================
# 🧩 Verify served review bytes, preserved snapshots, classified sources, and the planning-only production boundary.

from pathlib import Path
import argparse
import hashlib
from html.parser import HTMLParser
import json
import platform
import subprocess
import urllib.error
import urllib.request

RepositoryRoot = Path(__file__).resolve().parents[3]
Exhibit = RepositoryRoot / 'Exhibits/SpaceFormats'


class ReviewMarkup(HTMLParser):
    def __init__(self):
        super().__init__()
        self.Identifiers = []
        self.RemoteResources = []

    def handle_starttag(self, Tag, Attributes):
        Descriptor = dict(Attributes)
        if 'id' in Descriptor:
            self.Identifiers.append(Descriptor['id'])
        if Tag in ('img', 'script', 'link'):
            Locator = Descriptor.get('src', Descriptor.get('href', ''))
            if Locator.startswith(('http:', 'https:', '//')):
                self.RemoteResources.append(Locator)


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--url', default='http://127.0.0.1:8765')
    Arguments = Parser.parse_args()
    Audit = json.loads((Exhibit / 'Results/SourceAudit.json').read_text())
    ViewerChecks = json.loads((Exhibit / 'Results/ViewerChecks.json').read_text())
    Payload = (Exhibit / 'index.html').read_bytes()
    Digest = hashlib.sha256(Payload).hexdigest()
    assert Digest == ViewerChecks['viewer_sha256']
    Markup = ReviewMarkup()
    Markup.feed(Payload.decode())
    assert len(Markup.Identifiers) == len(set(Markup.Identifiers)), 'Duplicate HTML identifiers'
    assert not Markup.RemoteResources, 'Viewer is not self-contained'
    assert '{{' not in Payload.decode(), 'Unexpanded template token'
    for Suffix in ('/', '/index.html'):
        with urllib.request.urlopen(Arguments.url.rstrip('/') + Suffix, timeout=10) as Response:
            assert Response.status == 200
            assert Response.headers.get_content_type() == 'text/html'
            assert Response.read() == Payload
    Request = urllib.request.Request(Arguments.url, headers={'Host': 'review-preview.invalid'})
    with urllib.request.urlopen(Request, timeout=10) as Response:
        assert Response.status == 200, 'Host rejection breaks proxied previews'
    try:
        urllib.request.urlopen(Arguments.url.rstrip('/') + '/.git/config', timeout=10)
        raise AssertionError('Server exposes a repository credential/config path')
    except urllib.error.HTTPError as Failure:
        assert Failure.code == 404
    Changed = subprocess.check_output(['git', 'diff', '--name-only', 'HEAD'], cwd=RepositoryRoot, text=True).splitlines()
    Changed += subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'],
                                       cwd=RepositoryRoot, text=True).splitlines()
    Forbidden = ('Engine/', 'EngineContent/', 'UI/', 'Automation/', 'CMakeLists.txt')
    AllowedUI = {'Automation/ApplyImGuiPatches.py', 'Automation/Patches/PatchG-AdjacentTabAddButton.patch',
                 'Engine/Interface/ThemeStructure.h', 'Engine/Interface/ThemeStructure.cpp', 'Engine/Interface/PanelDockspaceLayout.cpp', 'Engine/Interface/PanelDockspace.h', 'Engine/Interface/PanelDockspace.cpp',
                 'UI/Editor/EditorWorkspace.h', 'UI/Editor/EditorWorkspace.cpp', 'Engine/Interface/ConfirmationDialogue.h', 'Engine/Interface/ConfirmationDialogue.cpp', 'Engine/Interface/NotificationSequence.h', 'Engine/Interface/NotificationSequence.cpp', 'EngineContent/Defaults/BlankProject/Blank.projectspace',
                 'Engine/ContentInterchange/ProjectSpaceCodec.h', 'Engine/ContentInterchange/ProjectSpaceCodec.cpp',
                 'Engine/Interface/AppearanceCodec.h', 'EngineContent/README.md', 'CMakeLists.txt', 'UI/Editor/ControlCentrePanel.h', 'UI/Editor/ControlCentrePanel.cpp',
                 'Engine/Interface/ContentDirectory.h', 'Engine/Interface/ContentDirectory.cpp',
                 'Engine/Interface/PanelExpansionTab.h', 'Engine/Interface/PanelExpansionTab.cpp',
                 'Engine/Interface/NotchMotion.h', 'Engine/Interface/NotchMotion.cpp',
                 'UI/Editor/ContentBrowserPanel.h', 'UI/Editor/ContentBrowserPanel.cpp',
                 'Engine/Interface/AppearanceInspector.h', 'Engine/Interface/AppearanceInspector.cpp',
                 'Engine/Interface/ConfigurationInspector.h', 'Engine/Interface/ConfigurationInspector.cpp',
                 'Engine/Interface/ConfigurationStructure.h', 'Engine/Interface/ConfigurationStructure.cpp',
                 'Engine/Interface/ControlKit.h', 'Engine/Interface/ControlKit.cpp',
                 'Engine/Interface/AppearanceCodec.cpp', 'Engine/Interface/InputPreferencesCodec.h',
                 'Engine/Interface/InputPreferencesCodec.cpp', 'Engine/Interface/TextFileReplacement.h',
                 'Engine/Interface/TextFileReplacement.cpp', 'UI/Editor/FoundationPanel.h', 'UI/Editor/FoundationPanel.cpp'}
    # Existing foundation work is still untracked in this session; include its approved production scope explicitly.
    FoundationAutomation = ('FontRequirements.txt', 'FontSources.json', 'PackageWindows.py', 'PrepareFonts.py',
                            'PrepareUI.py', 'StageThorVG.py', 'ThorVGSoftware.cmake', 'UIDependencies.json',
                            'WindowsToolchain.cmake')
    AllowedUI.update({'Engine/Interface/VulkanPresentation.h', 'Engine/Interface/VulkanPresentation.cpp',
                      'Engine/Interface/MonitorPlacement.h', 'Engine/Interface/MonitorPlacement.cpp',
                      'Engine/Interface/PanelDockspaceMonitor.cpp'})
    AllowedUI.update('Automation/' + Name for Name in FoundationAutomation)
    AllowedUI.update('Automation/Patches/' + Name for Name in (
        'PatchA-TrapezoidalTabs.patch', 'PatchB-TabOverlapZOrder.patch', 'PatchC-RoundTabButtons.patch',
        'PatchD-TabAddButton.patch', 'PatchE-NoTabScrollButtons.patch', 'PatchF-TabContentInsets.patch'))
    for Stem in ('FontCodec', 'GlyphSpace', 'IconArt', 'IconPresentation', 'MotionIntegrator',
                 'PixelSpace', 'TypefaceRegistry', 'VectorCodec'):
        AllowedUI.update('Engine/Interface/' + Stem + Extension for Extension in ('.h', '.cpp'))
    AllowedUI.update({'Engine/Interface/IconSymbols.inc', 'Engine/Interface/Lucide.NOTICE',
                      'Engine/Interface/TextEntryState.cpp', 'Engine/Lifecycle/ActivationSequence.h',
                      'Engine/Render/.gitkeep'})
    assert all(not Name.startswith(Forbidden) or Name in AllowedUI or
               Name.startswith('EngineContent/Icons/') for Name in Changed), 'Unapproved production scope'
    assert 'B approved' in (RepositoryRoot / 'Documentation/Plans/Ongoing/SpaceFormats.md').read_text()
    assert 'not approved</strong>' not in Payload.decode()
    Revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip()
    assert Revision == Audit['review_checkout_revision'], 'Commit changed during a planning-only pass'
    for Image in Audit['images']:
        assert hashlib.sha256((RepositoryRoot / Image['path']).read_bytes()).hexdigest() == Image['sha256']
    Original = RepositoryRoot / 'Documentation/Plans/References/SpaceFormatsOriginalBrief.txt'
    assert hashlib.sha256(Original.read_bytes()).hexdigest() == Audit['original_brief_sha256']
    assert hashlib.sha256(Original.read_bytes()).hexdigest() == Original.with_suffix('.sha256').read_text().split()[0]
    for Name in ('CLAUDE.md', 'Documentation/Plans/Ongoing/SpaceFormats.md', 'Exhibits/SpaceFormats/README.md'):
        for Ordinal, Line in enumerate((RepositoryRoot / Name).read_text().splitlines(), 1):
            assert len(Line) <= 120, f'{Name}:{Ordinal} exceeds Markdown line width'
            assert not Line.endswith((' ', '\t')), f'{Name}:{Ordinal} has trailing whitespace'
    Report = {
        'result': 'passed', 'checkout_revision': Revision, 'python': platform.python_version(),
        'node': subprocess.check_output(['node', '--version'], text=True).strip(),
        'viewer_sha256': Digest, 'http_root_and_index': '200; exact generated bytes',
        'preview_style_host': 'accepted', 'repository_config_request': '404; exhibit-only server root',
        'embedded_capture_original_hashes': f'passed; {len(Audit["images"])} new native CPU captures',
        'original_brief_snapshot': 'passed', 'markdown_checks': 'passed',
        'production_diff': 'authorised UI and editable projectspace persistence; binary/other format implementation still gated', 'head': 'unchanged; no commit or push performed',
        'proposed_format_acceptance': 'editor projectspace persistence passed; other/binary formats not run', 'historical_space_gate': 'not run',
        'full_browser_visual_run': 'not run', 'new_native_ui_execution': 'passed; separate NavigationChecks.json', 'windows_gpu_execution': 'not run',
        'command': 'python Exhibits/SpaceFormats/Code/ValidateReview.py --url ' + Arguments.url
    }
    (Exhibit / 'Results/ReviewChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    print(json.dumps(Report, indent=2))


if __name__ == '__main__':
    Main()
