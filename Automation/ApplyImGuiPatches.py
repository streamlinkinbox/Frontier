#============================================================================================================================================
#                                                        APPLYIMGUIPATCHES.PY
#============================================================================================================================================
# 🧩 Apply, verify, or revert the inherited ImGui patches and the adjacent/centred add-button correction in dependency order.
# Ported from Frontier- commit 338293863d7df4e585dec0c4acae45fe9c524ed5.
# Verified against lockfile pin 3bae66c735670619baf51391eba7f3d90a25d125 across Linux, Windows, and macOS (including FetchContent trees).

import os
import re
import sys

RepositoryRoot = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ImGuiRoot      = os.path.join(RepositoryRoot, 'ExternalPackages', 'imgui')
PatchRoot      = os.path.join(RepositoryRoot, 'Automation', 'Patches')

Declared = [
    {'Name': 'PatchA-TrapezoidalTabs.patch',      'Sentinel': 'FRONTIER PATCH A', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchB-TabOverlapZOrder.patch',     'Sentinel': 'FRONTIER PATCH B', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchC-RoundTabButtons.patch',      'Sentinel': 'FRONTIER PATCH C', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchD-TabAddButton.patch',         'Sentinel': 'FRONTIER PATCH D', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchE-NoTabScrollButtons.patch',   'Sentinel': 'FRONTIER PATCH E', 'Witness': 'imgui.cpp'},
    {'Name': 'PatchF-TabContentInsets.patch',     'Sentinel': 'FRONTIER PATCH F', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchG-AdjacentTabAddButton.patch', 'Sentinel': 'FRONTIER PATCH G', 'Witness': 'imgui_widgets.cpp'},
    {'Name': 'PatchH-SmoothOverscroll.patch',     'Sentinel': 'FRONTIER PATCH H', 'Witness': 'imgui.cpp'},
]

HunkHeaderRe = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')


def WriteReport(Tag, Message):
    print("{0} {1}".format("[{0}]".format(Tag).ljust(10), Message))


def ParseUnifiedPatch(PatchText):
    """Parses a unified diff into [{'Path': rel_path, 'Hunks': [[(op, text), ...], ...]}]."""
    Files = []
    Lines = PatchText.replace('\r\n', '\n').split('\n')
    Idx = 0
    Count = len(Lines)
    while Idx < Count:
        Line = Lines[Idx]
        if Line.startswith('--- '):
            OldSpec = Line[4:].strip().split('\t')[0]
            Idx += 1
            if Idx >= Count or not Lines[Idx].startswith('+++ '):
                raise RuntimeError('Malformed unified diff header near line {0}'.format(Idx))
            NewSpec = Lines[Idx][4:].strip().split('\t')[0]
            RelPath = NewSpec[2:] if NewSpec.startswith('b/') else (OldSpec[2:] if OldSpec.startswith('a/') else NewSpec)
            Idx += 1
            Hunks = []
            while Idx < Count and not Lines[Idx].startswith('--- ') and not Lines[Idx].startswith('diff --git '):
                Match = HunkHeaderRe.match(Lines[Idx])
                if Match:
                    OldStart = int(Match.group(1))
                    OldCount = int(Match.group(2)) if Match.group(2) is not None else 1
                    NewStart = int(Match.group(3))
                    NewCount = int(Match.group(4)) if Match.group(4) is not None else 1
                    Idx += 1
                    Ops = []
                    SeenOld = 0
                    SeenNew = 0
                    while Idx < Count and (SeenOld < OldCount or SeenNew < NewCount):
                        HLine = Lines[Idx]
                        if HLine.startswith('\\ No newline at end of file'):
                            Idx += 1
                            continue
                        Op = HLine[0] if HLine else ' '
                        Body = HLine[1:] if HLine else ''
                        if Op == ' ':
                            SeenOld += 1
                            SeenNew += 1
                        elif Op == '-':
                            SeenOld += 1
                        elif Op == '+':
                            SeenNew += 1
                        else:
                            break
                        Ops.append((Op, Body))
                        Idx += 1
                    Hunks.append({'OldStart': OldStart, 'NewStart': NewStart, 'Ops': Ops})
                else:
                    Idx += 1
            Files.append({'Path': RelPath, 'Hunks': Hunks})
        else:
            Idx += 1
    return Files


def ApplyParsedPatch(TargetRoot, ParsedFiles, Reverse=False, DryRun=False):
    StagedWrites = {}
    for FileEntry in ParsedFiles:
        FullPath = os.path.join(TargetRoot, FileEntry['Path'])
        if not os.path.isfile(FullPath):
            return False, 'missing target file {0}'.format(FullPath)
        RawText = StagedWrites.get(FullPath)
        if RawText is None:
            with open(FullPath, 'r', encoding='utf-8') as Handle:
                RawText = Handle.read().replace('\r\n', '\n')
        HasTrailingNewline = RawText.endswith('\n')
        FileLines = RawText[:-1].split('\n') if HasTrailingNewline else RawText.split('\n')
        SearchStart = 0

        for Hunk in FileEntry['Hunks']:
            Expected = []
            Replacement = []
            for Op, Body in Hunk['Ops']:
                EffOp = ('-' if Op == '+' else ('+' if Op == '-' else ' ')) if Reverse else Op
                if EffOp in (' ', '-'):
                    Expected.append(Body)
                if EffOp in (' ', '+'):
                    Replacement.append(Body)

            Hint = max(0, (Hunk['NewStart'] if Reverse else Hunk['OldStart']) - 1)
            ExpLen = len(Expected)
            MatchedAt = None

            # Search outward from Hint (>= SearchStart) using rstrip() tolerance for CRLF/whitespace safety
            Candidates = [Pos for Pos in range(SearchStart, len(FileLines) - ExpLen + 1)]
            Candidates.sort(key=lambda Pos: abs(Pos - Hint))
            for Pos in Candidates:
                if all(FileLines[Pos + Off].rstrip() == Expected[Off].rstrip() for Off in range(ExpLen)):
                    MatchedAt = Pos
                    break

            if MatchedAt is None:
                return False, 'hunk at line {0} did not match in {1}'.format(Hint + 1, FileEntry['Path'])

            FileLines = FileLines[:MatchedAt] + Replacement + FileLines[MatchedAt + ExpLen:]
            SearchStart = MatchedAt + len(Replacement)

        StagedWrites[FullPath] = '\n'.join(FileLines) + ('\n' if HasTrailingNewline else '')

    if not DryRun:
        for FullPath, Content in StagedWrites.items():
            with open(FullPath, 'w', encoding='utf-8', newline='\n') as Handle:
                Handle.write(Content)
    return True, ''


def Main(Arguments):
    global ImGuiRoot
    for Idx, Arg in enumerate(Arguments):
        if Arg == '--imgui-root' and Idx + 1 < len(Arguments):
            ImGuiRoot = os.path.abspath(Arguments[Idx + 1])
        elif Arg.startswith('--imgui-root='):
            ImGuiRoot = os.path.abspath(Arg.split('=', 1)[1])

    Revert = '--revert' in Arguments
    Verify = '--verify' in Arguments

    if not os.path.isfile(os.path.join(ImGuiRoot, 'imgui.cpp')):
        WriteReport('FAILED', 'the ImGui source tree is not present at {0}'.format(ImGuiRoot))
        return 1

    Ordered = list(reversed(Declared)) if Revert else Declared

    for Entry in Ordered:
        PatchName = Entry['Name']
        PatchPath = os.path.join(PatchRoot, PatchName)

        if not os.path.isfile(PatchPath):
            WriteReport('FAILED', 'declared patch {0} is absent from {1}'.format(PatchName, PatchRoot))
            return 1

        WitnessPath = os.path.join(ImGuiRoot, Entry['Witness'])
        with open(WitnessPath, 'r', encoding='utf-8', errors='replace') as Reader:
            AlreadyApplied = Entry['Sentinel'] in Reader.read()

        if Verify:
            if AlreadyApplied:
                WriteReport('ImGui', '{0} is applied'.format(PatchName))
            else:
                WriteReport('FAILED', '{0} is NOT applied'.format(PatchName))
                return 1
            continue

        with open(PatchPath, 'r', encoding='utf-8') as PatchHandle:
            Parsed = ParseUnifiedPatch(PatchHandle.read())

        if Revert:
            if not AlreadyApplied:
                WriteReport('SKIP', '{0} was not applied'.format(PatchName))
                continue
            Ok, Diag = ApplyParsedPatch(ImGuiRoot, Parsed, Reverse=True, DryRun=False)
            if not Ok:
                WriteReport('FAILED', 'could not revert {0}: {1}'.format(PatchName, Diag))
                return 1
            WriteReport('ImGui', 'reverted {0}'.format(PatchName))
            continue

        if AlreadyApplied:
            WriteReport('SKIP', '{0} already applied'.format(PatchName))
            continue

        Ok, Diag = ApplyParsedPatch(ImGuiRoot, Parsed, Reverse=False, DryRun=True)
        if not Ok:
            WriteReport('FAILED', '{0} does not apply cleanly: {1}'.format(PatchName, Diag))
            return 1

        Ok, Diag = ApplyParsedPatch(ImGuiRoot, Parsed, Reverse=False, DryRun=False)
        if not Ok:
            WriteReport('FAILED', 'could not apply {0}: {1}'.format(PatchName, Diag))
            return 1

        WriteReport('ImGui', 'applied {0}'.format(PatchName))

    return 0


if __name__ == '__main__':
    sys.exit(Main(sys.argv[1:]))
