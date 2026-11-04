#============================================================================================================================================
#                                                         VALIDATENAVIGATION.PY
#============================================================================================================================================
# 🧩 Build production UI, exercise pointer navigation, capture native commands, and inspect the Windows package.

from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import xml.etree.ElementTree as Xml

RepositoryRoot = Path(__file__).resolve().parents[3]
Exhibit = RepositoryRoot / 'Exhibits/ControlCentre'
Results = Exhibit / 'Results'


def Digest(Payload):
    return hashlib.sha256(Payload).hexdigest()


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--baseline', type=Path, default=RepositoryRoot / '_ClaudeScratch/tmp/upstream')
    Parser.add_argument('--debug', type=Path, default=RepositoryRoot / '_ClaudeScratch/build/ui')
    Parser.add_argument('--sanitized', type=Path, default=RepositoryRoot / '_ClaudeScratch/build/ui-sanitized')
    Parser.add_argument('--windows', type=Path, default=RepositoryRoot / '_ClaudeScratch/build/windows-ui')
    Arguments = Parser.parse_args()
    Environment = dict(os.environ)
    Environment['PATH'] = str(Path(sys.executable).parent) + os.pathsep + Environment['PATH']
    Report = {
        'date': '2026-10-17', 'result': 'running',
        'checkout_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=RepositoryRoot, text=True).strip(),
        'uncommitted_changes': True, 'host': platform.platform(), 'python': platform.python_version(),
        'scope': 'Native production ImGui UI on Linux verification host; Windows package is cross-compiled only.',
        'commands': [], 'tests': {}, 'images': [],
        'limitations': ['No Windows startup, GPU, DPI/device, detached OS-window or native MSVC execution.',
                        'Embedded layouts, monitor recovery and Vulkan platform-window rendering are implemented; physical Windows GPU/monitor execution remains unverified.',
                        'No full browser visual run; the HTML is an evidence viewer.',
                        'Editable projectspace preferences implemented; binary FSPC runtime codecs and other Space formats remain unimplemented.',
                        'Keyboard commands execute in the UI host; pointer gain/inversion have an applied preview, not a scene camera.',
                        'Notification preferences persist for projectspace saves; legacy TOML compatibility hosts retain session-only notifications.',
                        'Filtered reference: no GI/Render/Materials categories, renderer tiles or render-scale pill.',
                        'Content browser shows file metadata/type glyphs, not content thumbnails; no material or scene runtime.',
                        'Filename matching folds ASCII case only; paths and literal Unicode queries remain UTF-8.',
                        'Read-only filesystem confinement is not a hostile concurrent-filesystem security boundary.']
    }

    def Run(Command, LogName):
        Completed = subprocess.run([str(Part) for Part in Command], cwd=RepositoryRoot, env=Environment,
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (Results / LogName).write_text(Completed.stdout)
        Report['commands'].append({'argv': [str(Part) for Part in Command], 'exit_code': Completed.returncode,
                                   'log': 'Results/' + LogName})
        print(LogName + ': exit ' + str(Completed.returncode), flush=True)
        if Completed.returncode:
            raise RuntimeError(LogName + ' failed; see the retained output')

    (Results / 'NavigationChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    try:
        Report['configuration'] = {}
        Keys = {'CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_FLAGS', 'CMAKE_EXE_LINKER_FLAGS',
                'CMAKE_TOOLCHAIN_FILE', 'FRONTIER_HOST_VERIFICATION'}
        for Name, Build in [('Debug', Arguments.debug), ('Sanitized', Arguments.sanitized),
                            ('Windows', Arguments.windows)]:
            Configuration = {}
            for Line in (Build / 'CMakeCache.txt').read_text().splitlines():
                if '=' in Line and ':' in Line.split('=', 1)[0]:
                    Key = Line.split(':', 1)[0]
                    if Key in Keys:
                        Configuration[Key] = Line.split('=', 1)[1]
            Report['configuration'][Name] = Configuration
        assert '-fsanitize=address,undefined' in Report['configuration']['Sanitized']['CMAKE_CXX_FLAGS']
        assert Report['configuration']['Windows']['CMAKE_BUILD_TYPE'] == 'Release'
        Pins = json.loads((Results / 'NavigationReferencePins.json').read_text())
        for Pin in Pins['sources']:
            Payload = (Arguments.baseline / Pin['path']).read_bytes()
            assert Digest(Payload) == Pin['sha256']
            assert hashlib.sha1(b'blob ' + str(len(Payload)).encode() + b'\0' + Payload).hexdigest() == Pin['git_blob']
        Report['reference_pins'] = 'passed; ' + str(len(Pins['sources'])) + ' approved baseline files'
        Report['reference_commit'] = Pins['commit']
        RangeHeader = (RepositoryRoot / 'Engine/Interface/ControlKit.h').read_text()
        assert 'SliderThin' not in RangeHeader and 'HighlightFill' not in RangeHeader
        assert 'IsDirty' not in (RepositoryRoot / 'Engine/Interface/ConfigurationInspector.h').read_text()
        for Folder in ('Engine', 'UI'):
            for Source in (RepositoryRoot / Folder).rglob('*.cpp'):
                assert 'ImGui::Slider' not in Source.read_text(), 'A native slider bypasses the shared styling'
        Report['single_slider_style'] = 'passed; one 28 px rail / 26 px thumb contract; no alternate/native slider paths'
        Report['footer_contour'] = 'native CPU pixel assertions: straight upper seam and contained lower corners'
        Report['input_checks'] = ['chord capture/conflict/cancel/focus loss', 'exact modifiers and real command execution',
                                  'default/custom profiles and draft/apply/discard/defaults',
                                  'applied pointer response and persistent reload', 'Unicode/schema/refusal/failure cleanup',
                                  '375x700 viewport at 125 percent scale']
        for Name, Build, LogName in [('Debug', Arguments.debug, 'LocalChecks.txt'),
                                      ('Sanitized', Arguments.sanitized, 'SanitizedChecks.txt')]:
            Run(['cmake', '--build', Build, '-j', '4'], 'Navigation' + Name + 'Build.txt')
            Junit = Results / ('Navigation' + Name + '.xml')
            Run(['ctest', '--test-dir', Build, '--output-on-failure', '--output-junit', Junit], LogName)
            Root = Xml.parse(Junit).getroot()
            Report['tests'][Name] = dict(Root.attrib)
            assert int(Root.attrib['tests']) == 14 and int(Root.attrib['failures']) == 0
        Report['content_browser_checks'] = ['real-files-only; root-confined navigation and bounded enumeration',
                                             'Back/Forward/Up/Root/Reload; single/double click and arrow keys',
                                             'search/restore; type categories; grid/list; scroll; metadata selection',
                                             'missing/empty folders; symlinks omitted; stale selection cleared',
                                             'shared theme/fonts; 375x700 at 125 percent; popup input ownership',
                                             'reflected shared expansion geometry/gesture parity; maximize/restore; focus cancellation']
        Names = ['ClosedNotch', 'QuickPanel', 'Settings', 'SettingsTransition', 'Input', 'Notifications',
                 'InputCapture', 'InputConflict', 'InputConfigured', 'InputPointerResponse',
                 'Fonts', 'Display', 'TypeScale', 'Theme', 'LightTheme', 'SwipeUp']
        for Name in Names:
            Source = Arguments.debug / 'control-centre-evidence' / (Name + '.ppm')
            Destination = Exhibit / 'Images' / (Name + '.png')
            # Same lossless PPM -> PNG encoder as the original native exhibit.
            subprocess.run([sys.executable, str(RepositoryRoot / 'Exhibits/UIFoundation/Code/EncodeEvidence.py'),
                            str(Source), str(Destination)], check=True, cwd=RepositoryRoot)
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'ControlCentreValidation -> actual ImGui commands -> CPU fixture -> PNG'})
        for Name in ['BrowserRoot', 'BrowserSearch', 'BrowserSelection', 'BrowserEmpty',
                     'BrowserLight', 'BrowserNarrow', 'BrowserDrag']:
            Source = Arguments.debug / 'content-browser-evidence' / (Name + '.ppm')
            Destination = RepositoryRoot / 'Exhibits/ContentBrowser/Images' / (Name + '.png')
            subprocess.run([sys.executable, str(RepositoryRoot / 'Exhibits/UIFoundation/Code/EncodeEvidence.py'),
                            str(Source), str(Destination)], check=True, cwd=RepositoryRoot)
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'ContentBrowserValidation -> actual ImGui commands -> CPU fixture -> PNG'})
        for Name in ['ProjectAppearance', 'ProjectInput', 'ProjectNotifications', 'EmergencyFont']:
            Source = Arguments.debug / 'projectspace-evidence' / (Name + '.ppm')
            Destination = RepositoryRoot / 'Exhibits/ProjectSpace/Images' / (Name + '.png')
            Destination.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([sys.executable, str(RepositoryRoot / 'Exhibits/UIFoundation/Code/EncodeEvidence.py'),
                            str(Source), str(Destination)], check=True, cwd=RepositoryRoot)
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'ProjectSpaceValidation -> actual ImGui commands -> CPU fixture -> PNG'})
        for Name in ['SavedTopRight', 'SaveError', 'NotificationHistory', 'NotificationNarrow', 'CardsLight', 'CardsDark', 'ConfirmApply', 'ConfirmSemantic', 'StreamNarrow']:
            Source = Arguments.debug / 'notification-evidence' / (Name + '.ppm')
            Destination = RepositoryRoot / 'Exhibits/Notifications/Images' / (Name + '.png')
            Destination.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([sys.executable, str(RepositoryRoot / 'Exhibits/UIFoundation/Code/EncodeEvidence.py'),
                            str(Source), str(Destination)], check=True, cwd=RepositoryRoot)
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'NotificationValidation -> actual ImGui commands -> CPU fixture -> PNG'})
        Report['notification_checks'] = ['top-right card stack outside shared expansion tab',
            'real preference save/apply/error events and persisted suppression/hold preferences',
            '64-entry history, 16 pending, three visible; dismissed/expired/suppressed alerts retain history',
            'visible-only expiry; hover pause; finite durations/time; UTF-8-safe storage limits',
            'matching close-button press/release; no drawer click-through; native Clear history in Quick Controls',
            'responsive Quick Controls split/stack; Notification Stream removed from notification settings',
            'shared 200x18 logical px trapezoidal tabs; unboxed Invert Y-Axis switch',
            'real Apply/Cancel modal; no save on request/cancel; Escape, default Cancel, Tab+Enter and pointer confinement',
            'light/dark semantic card fixtures explicitly labelled; 375x700 at 125 percent',
            'nine native CPU captures; rounded actual-save and semantic-header fixture; Windows runtime/GPU not executed']
        Report['projectspace_checks'] = ['single editor container; no TOML sidecars; shared table schemas',
            'fresh-identity template snapshot; no overwrite; Unicode paths; confined root locators',
            'Personal/Project saves; optional overrides; notification reload; dirty-draft refusal',
            'unknown/versioned/oversized/binary refusal; malformed pins; failed writes preserve previous bytes',
            'catalogue/extension retention; missing profiles/family and emergency built-in font',
            'native scope/Apply pointer dispatch and four native captures']
        Run(['cmake', '--build', Arguments.windows, '-j', '4'], 'NavigationWindowsBuild.txt')
        Run([sys.executable, 'Automation/PackageWindows.py', '--build', Arguments.windows,
             '--toolchain', 'Zig 0.13.0; x86_64-windows-gnu; Release; cross-compiled on Linux'],
            'NavigationWindowsPackaging.txt')
        Run([sys.executable, 'Exhibits/ControlCentre/Code/InspectWindowsPackage.py'], 'NavigationWindowsInspection.txt')
        for Name in ['DraggableDefault', 'DockedContent', 'DockedStream', 'SplitPanels', 'WorkspaceSettings']:
            Source = Arguments.debug / 'docking-evidence' / (Name + '.ppm')
            Destination = RepositoryRoot / 'Exhibits/Docking/Images' / (Name + '.png')
            Run([sys.executable, 'Exhibits/UIFoundation/Code/EncodeEvidence.py', Source, Destination],
                'Navigation' + Name + 'Encoding.txt')
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'DockingValidation -> actual ImGui commands -> CPU fixture -> PNG'})
        Report['docking_checks'] = ['patched native dockspace; draggable browser default; Apply/Cancel switching preserves one original browser',
            'pointer-driven plus/catalogue/native tab selection/native close/native reorder; native builder splitting and dock-back',
            'native plus disc/hitbox adjacency and rendered cross centring at 100/125%; opaque darkest drawer/tab pixels across 8 themes; browser state retained',
            'enabled/disabled personal reload; project-policy isolation; malformed hosting refused; missing adapters refuse detachment; no OS-window, native drag/split or monitor execution']
        for Name in ['RestoredTabs', 'RestoredSplit']:
            Source = Arguments.debug / 'workspace-layout-evidence' / (Name + '.ppm')
            Destination = RepositoryRoot / 'Exhibits/Docking/Images' / (Name + '.png')
            Run([sys.executable, 'Exhibits/UIFoundation/Code/EncodeEvidence.py', Source, Destination],
                'Navigation' + Name + 'Encoding.txt')
            Report['images'].append({'path': Destination.relative_to(RepositoryRoot).as_posix(),
                                     'sha256': Digest(Destination.read_bytes()),
                                     'producer': 'WorkspaceLayoutValidation -> fresh-context real native restore -> CPU commands -> PNG'})
        Report['workspace_layout_checks'] = ['Frontier-owned bounded TOML inside personal projectspace; native DockBuilder restore; no raw INI load or sidecar',
            'fresh contexts: tab order/selection, split ratios, floating groups/geometry, closed panels, draggable browser visibility',
            'two-second stable debounce; no unchanged-file rewrite; final shutdown save; preserves unrelated drafts/extensions/overrides',
            'version/schema/cycle/size refusal; guarded default fallback; native reset button; atomic-write failure preserves previous bytes',
            'synthetic native-window archive returns panels to main when unsupported/disabled; no real OS-window or monitor execution']
        Report['monitor_recovery_checks'] = ['removed/negative monitor coordinates; saved/current DPI; resolution/taskbar fitting',
            'idempotence, invalid geometry and transient enumeration',
            'synthetic platform move/resize callbacks; recovery deferred for minimized secondary windows',
            'optional layout monitor metadata validated; no physical OS/GPU execution']
        Report['vulkan_display'] = 'Windows default binary: Vulkan/GLFW; DX11 comparison isolated; PE imports inspected, runtime unverified'
        Report['windows_package'] = json.loads((Results / 'WindowsPackage.json').read_text())
        Report['source_sha256'] = {}
        Sources = [RepositoryRoot / 'Automation/ApplyImGuiPatches.py',
                   RepositoryRoot / 'Automation/Patches/PatchG-AdjacentTabAddButton.patch',
                   RepositoryRoot / 'Exhibits/UIFoundation/Code/WindowsUI.cpp', RepositoryRoot / 'CMakeLists.txt', RepositoryRoot / 'EngineContent/Defaults/BlankProject/Blank.projectspace']
        for Folder in ('Engine', 'UI', 'Exhibits/ControlCentre/Code', 'Exhibits/ContentBrowser/Code', 'Exhibits/ProjectSpace/Code', 'Exhibits/Notifications/Code', 'Exhibits/Docking/Code'):
            Sources.extend(File for File in (RepositoryRoot / Folder).rglob('*')
                           if File.is_file() and File.suffix in ('.h', '.cpp', '.py'))
        for Source in sorted(Sources):
            Report['source_sha256'][Source.relative_to(RepositoryRoot).as_posix()] = Digest(Source.read_bytes())
        Report['result'] = 'passed'
    except Exception as Failure:
        Report['result'] = 'failed'
        Report['error'] = str(Failure)
        raise
    finally:
        (Results / 'NavigationChecks.json').write_text(json.dumps(Report, indent=2) + '\n')
    print('PASS: reference pins, Debug/sanitizer tests, native navigation captures and Windows package inspection.')


if __name__ == '__main__':
    Main()
