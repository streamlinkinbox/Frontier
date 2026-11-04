#============================================================================================================================================
#                                                            BUILDREVIEW.PY
#============================================================================================================================================
# 🧩 Pin inspected source bytes and embed existing native evidence into a self-contained planning review page.

from pathlib import Path
import argparse
import base64
import hashlib
import html
import json
import re
import struct
import subprocess

RepositoryRoot = Path(__file__).resolve().parents[3]
Exhibit = RepositoryRoot / 'Exhibits/SpaceFormats'
BaselineCommit = '338293863d7df4e585dec0c4acae45fe9c524ed5'
OriginalPath = RepositoryRoot / 'Documentation/Plans/References/SpaceFormatsOriginalBrief.txt'
PlanPath = RepositoryRoot / 'Documentation/Plans/Ongoing/SpaceFormats.md'


def Digest(Payload):
    return hashlib.sha256(Payload).hexdigest()


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--baseline', type=Path, default=RepositoryRoot / '_ClaudeScratch/tmp/upstream')
    Arguments = Parser.parse_args()
    Pins = json.loads((Exhibit / 'Results/BaselinePins.json').read_text())
    assert Pins['repository'] == 'SultanAladin/Frontier-' and Pins['commit'] == BaselineCommit
    Sources = []
    SourceText = {}
    for Pin in Pins['sources']:
        Payload = (Arguments.baseline / Pin['path']).read_bytes()
        Blob = hashlib.sha1(b'blob ' + str(len(Payload)).encode() + b'\0' + Payload).hexdigest()
        assert Blob == Pin['git_blob'], 'Pinned source differs: ' + Pin['path']
        SourceText[Pin['path']] = Payload.decode('utf-8')
        Sources.append(dict(Pin, sha256=Digest(Payload), bytes=len(Payload),
                            url=f'https://github.com/{Pins["repository"]}/blob/{BaselineCommit}/{Pin["path"]}'))
    Findings = json.loads((Exhibit / 'Code/AuditFindings.json').read_text())
    for Finding in Findings:
        assert Finding['classification'] in ('confirmed', 'suspected', 'proposed requirement')
        assert 1 <= Finding['first_line'] <= Finding['last_line'] <= len(SourceText[Finding['source']].splitlines())
        Finding['url'] = (f'https://github.com/{Pins["repository"]}/blob/{BaselineCommit}/'
                          f'{Finding["source"]}#L{Finding["first_line"]}-L{Finding["last_line"]}')
    assert len(re.findall(r'\{ SpaceTag\("[A-Z]{4}"\).*?"\.[a-z]+"',
                          SourceText['Engine/ContentInterchange/SpaceFormat.h'])) == 13
    assert 'sizeof(SpaceHeader) == 20u' in SourceText['Engine/ContentInterchange/SpaceFormat.h']
    assert 'bool ConsoleHost::SaveDocument(' in SourceText[
        'Editor/AuthoringTools/Modelling/SolidArc/Console/ConsoleHost.cpp']
    CurrentSources = []
    for Name in ('CLAUDE.md', 'Engine/Interface/FontCodec.h', 'Engine/Interface/FontCodec.cpp',
                 'Engine/Interface/TypefaceRegistry.h', 'Engine/Interface/TypefaceRegistry.cpp',
                 'Engine/Interface/AppearanceCodec.cpp', 'Automation/PrepareFonts.py'):
        CurrentSources.append({'path': Name, 'sha256': Digest((RepositoryRoot / Name).read_bytes()),
                               'origin': 'Current Frontier checkout, not the pinned Frontier- baseline'})
    Labels = [('ClosedNotch', 'Collapsed expansion tab'), ('QuickPanel', 'Quick controls - circular toggles'),
              ('Settings', 'Settings - category list'), ('SettingsTransition', 'Settings - animated page entry'),
              ('Input', 'Input - editable preferences'),
              ('InputCapture', 'Input - native keyboard chord capture'),
              ('InputConflict', 'Input - duplicate shortcut rejected'),
              ('InputConfigured', 'Input - applied and persisted'),
              ('InputPointerResponse', 'Input - applied pointer response'),
              ('Notifications', 'Notifications - working session preferences'),
              ('Fonts', 'Appearance - Typography'), ('Display', 'Appearance - interface scale'),
              ('TypeScale', 'Typography - type scale'), ('Theme', 'Appearance - Theme'),
              ('LightTheme', 'Applied light theme'), ('SwipeUp', 'Upward drag - travelling boundary')]
    BrowserLabels = [('BrowserRoot', 'Content Browser - real EngineContent directories'),
                     ('BrowserSearch', 'Content Browser - live filename search'),
                     ('BrowserSelection', 'Content Browser - SVG selection and file details'),
                     ('BrowserEmpty', 'Content Browser - empty filtered results'),
                     ('BrowserLight', 'Content Browser - shared applied light theme'),
                     ('BrowserNarrow', 'Content Browser - 375x700 at 125 percent'),
                     ('BrowserDrag', 'Content Browser - downward pointer carry')]
    ProjectLabels = [('ProjectAppearance', 'Projectspace - embedded appearance and explicit save scope'),
                     ('ProjectInput', 'Projectspace - saved input profile'),
                     ('ProjectNotifications', 'Projectspace - saved notification preferences'),
                     ('EmergencyFont', 'Projectspace - built-in emergency font with no packaged faces')]
    NotificationLabels = [('SavedTopRight', 'Notifications - actual preference save, top-right card'),
                          ('SaveError', 'Notifications - actual refused save, error card'),
                          ('NotificationHistory', 'Quick Controls - Notification Stream beside the tiles'),
                          ('NotificationNarrow', 'Notifications - 375x700 viewport at 125 percent'),
                          ('CardsLight', 'Notifications - light semantic fixtures, production renderer'),
                          ('CardsDark', 'Notifications - dark semantic fixtures, production renderer'),
                          ('ConfirmApply', 'Confirmation - actual preference Apply / Cancel popup'),
                          ('ConfirmSemantic', 'Confirmation - semantic-header fixture, no invented operation'),
                          ('StreamNarrow', 'Quick Controls - stacked Notification Stream at 125 percent')]
    DockingLabels = [('DraggableDefault', 'Workspace - draggable browser trapezoid is the default'),
                     ('DockedContent', 'Docking - original Content Browser in native patched tab'),
                     ('DockedStream', 'Docking - native Notification Stream tab selection'),
                     ('SplitPanels', 'Docking - native builder split retaining original content'),
                     ('WorkspaceSettings', 'Workspace - hosting preferences and saved-layout reset'),
                     ('RestoredTabs', 'Workspace layout - tab order and selection restored in a fresh context'),
                     ('RestoredSplit', 'Workspace layout - split proportions restored in a fresh context')]
    Images = []
    for Name, Label in Labels + BrowserLabels + ProjectLabels + NotificationLabels + DockingLabels:
        Folder = 'Docking' if Name in dict(DockingLabels) else 'Notifications' if Name in dict(NotificationLabels) else 'ProjectSpace' if Name in dict(ProjectLabels) else 'ContentBrowser' if Name.startswith('Browser') else 'ControlCentre'
        Source = RepositoryRoot / f'Exhibits/{Folder}/Images/{Name}.png'
        Payload = Source.read_bytes()
        assert Payload[:8] == b'\x89PNG\r\n\x1a\n'
        Width, Height = struct.unpack('>II', Payload[16:24])
        assert (Width, Height) == ((375, 700) if Name in ('BrowserNarrow', 'BrowserDrag', 'NotificationNarrow', 'StreamNarrow') else (1100, 1000))
        Images.append({'path': Source.relative_to(RepositoryRoot).as_posix(), 'label': Label,
                       'sha256': Digest(Payload), 'width': Width, 'height': Height,
                       'uri': 'data:image/png;base64,' + base64.b64encode(Payload).decode()})
    Native = json.loads((RepositoryRoot / 'Exhibits/ControlCentre/Results/NavigationChecks.json').read_text())
    assert Native['result'] == 'passed', 'New native verification has not passed'
    for Name, Hash in Native['source_sha256'].items():
        assert Digest((RepositoryRoot / Name).read_bytes()) == Hash, 'Stale native source evidence: ' + Name
    for Image in Native['images']:
        assert Digest((RepositoryRoot / Image['path']).read_bytes()) == Image['sha256'], 'Stale native capture'
    NavigationPins = json.loads((RepositoryRoot / 'Exhibits/ControlCentre/Results/NavigationReferencePins.json').read_text())
    Original = OriginalPath.read_bytes()
    assert Digest(Original) == OriginalPath.with_suffix(".sha256").read_text().split()[0], "Original brief snapshot changed"
    Plan = PlanPath.read_bytes()
    Revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip()
    Report = {'format': 1, 'review_date': '2026-10-17', 'baseline_repository': Pins['repository'],
              'baseline_commit': BaselineCommit, 'baseline_tree': Pins['tree_sha'],
              'review_checkout_revision': Revision,
              'uncommitted_paths': subprocess.check_output(['git', 'status', '--porcelain'],
                                                           cwd=RepositoryRoot, text=True).splitlines(),
              'scope': 'Format specification plus separately authorised native UI correction; not a security certification.',
              'original_brief_sha256': Digest(Original), 'refined_plan_sha256': Digest(Plan),
              'sources': Sources, 'current_sources': CurrentSources, 'findings': Findings,
              'navigation_reference': NavigationPins,
              'native_ui_report': 'Exhibits/ControlCentre/Results/NavigationChecks.json',
              'native_ui_report_sha256': Digest((RepositoryRoot / 'Exhibits/ControlCentre/Results/NavigationChecks.json').read_bytes()),
              'images': [{Key: Content for Key, Content in Image.items() if Key != 'uri'} for Image in Images],
              'checks': {'baseline_git_blob_pins': 'passed', 'finding_line_ranges': 'passed',
                         'baseline_type_count_and_header': 'passed', 'existing_arc_save_path': 'passed',
                         'native_png_headers_and_dimensions': 'passed',
                         'historical_space_gate': 'not run', 'proposed_format_acceptance': 'editor projectspace persistence passed; binary/other format acceptance not run',
                         'new_native_ui_execution': 'passed; ControlCentreValidation, ContentBrowserValidation, ProjectSpaceValidation, NotificationValidation, DockingValidation and WorkspaceLayoutValidation; see ControlCentre/Results/NavigationChecks.json',
                         'windows_runtime_and_gpu': 'not run'},
              'limitations': ['Static findings are not exploitation tests or runtime safety certification.',
                              'Browser evidence viewer is not a native editor implementation.',
                              'B approved: editable authoring sources remain separate from compiled binary runtime packages.',
                              'Editable projectspace persistence is implemented; binary runtime/other Space codecs remain gated.',
                              'Input shortcuts execute UI commands; pointer settings have a response preview, not scene-camera integration.',
                              'Top-right cards, Quick Controls Notification Stream and Apply/Cancel dialogues are implemented; action callbacks and independent fatal presentation remain pending.']}
    (Exhibit / 'Results/SourceAudit.json').write_text(json.dumps(Report, indent=2, ensure_ascii=False) + '\n')
    Escaped = html.escape
    AuditRows = ''.join('<tr><td>' + Escaped(F['id']) + '</td><td><span class="badge">' +
                        Escaped(F['classification']) + '</span></td><td><strong>' + Escaped(F['summary']) +
                        '</strong><p>' + Escaped(F['correction']) + '</p><a target="_blank" rel="noopener" href="' +
                        Escaped(F['url'], quote=True) + '">Pinned source · lines ' + str(F['first_line']) + '–' +
                        str(F['last_line']) + '</a></td></tr>' for F in Findings)
    SourceRows = ''.join('<tr><td><a target="_blank" rel="noopener" href="' + S['url'] + '">' +
                         Escaped(S['path']) + '</a></td><td><code>' + S['sha256'] + '</code></td></tr>' for S in Sources)
    NavigationRows = ''.join('<tr><td><a target="_blank" rel="noopener" href="' + Pin['url'] + '">' +
                             Escaped(Pin['path']) + '</a></td><td><code>' + Pin['sha256'] + '</code></td></tr>'
                             for Pin in NavigationPins['sources'])
    InitialIndex = 29  # Actual responsive Quick Controls Notification Stream.
    Thumbnails = ''.join('<button class="capture' + (' selected' if Index == InitialIndex else '') +
                         '" data-image="' + str(Index) + '" aria-pressed="' + ('true' if Index == InitialIndex else 'false') +
                         '"><img alt="' + Escaped(Image['label']) + '" src="' + Image['uri'] + '"><span>' +
                         Escaped(Image['label']) + '</span></button>' for Index, Image in enumerate(Images))
    OriginalURI = 'data:text/plain;base64,' + base64.b64encode(Original).decode()
    PlanURI = 'data:text/markdown;base64,' + base64.b64encode(Plan).decode()
    Template = (Exhibit / 'Code/ReviewTemplate.html').read_text()
    Replacements = {'{{MATERIAL_REFERENCE_IMAGE}}': 'data:image/png;base64,' + base64.b64encode(
                        (RepositoryRoot / 'Exhibits/TextureStudio/Images/ReferenceMaterialWorkspace.png').read_bytes()).decode(),
                    '{{MATERIAL_WORKSPACE_IMAGE}}': 'data:image/png;base64,' + base64.b64encode(
                        (RepositoryRoot / 'Exhibits/TextureStudio/Images/MaterialWorkspace.png').read_bytes()).decode(),
                    '{{MATERIAL_LIBRARY_IMAGE}}': 'data:image/png;base64,' + base64.b64encode(
                        (RepositoryRoot / 'Exhibits/TextureStudio/Images/MaterialLibrary.png').read_bytes()).decode(),
                    '{{AUDIT_ROWS}}': AuditRows, '{{SOURCE_ROWS}}': SourceRows, '{{THUMBNAILS}}': Thumbnails,
                    '{{NAVIGATION_ROWS}}': NavigationRows, '{{CAPTURE_COUNT}}': str(len(Images)),
                    '{{UI_REFERENCE_COUNT}}': str(len(NavigationPins['sources'])),
                    '{{FIRST_LABEL}}': Escaped(Images[InitialIndex]['label']), '{{FIRST_IMAGE}}': Images[InitialIndex]['uri'], '{{PLAN}}': Escaped(Plan.decode()),
                    '{{ORIGINAL}}': Escaped(Original.decode()), '{{ORIGINAL_URI}}': OriginalURI,
                    '{{PLAN_URI}}': PlanURI, '{{ORIGINAL_HASH}}': Digest(Original), '{{REVISION}}': Revision[:7]}
    for Token, Content in Replacements.items():
        assert Token in Template, Token
        Template = Template.replace(Token, Content)
    assert '{{' not in Template
    (Exhibit / 'index.html').write_text(Template)
    print(f'PASS: {len(Sources)} baseline pins, {len(Findings)} classified findings, {len(Images)} embedded PNGs.')
    print('Original brief SHA-256:', Digest(Original))
    print('Review:', Exhibit / 'index.html')


if __name__ == '__main__':
    Main()
