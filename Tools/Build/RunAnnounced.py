#!/usr/bin/env python3
"""Run a command, stream its output, and on failure put the tail into a GitHub Actions annotation.

    python3 Tools/Build/RunAnnounced.py <label> -- <command> [arguments...]

Workflow logs live behind a storage host that is not always reachable from where the agent runs, but
annotations come back through the ordinary API. So a failing step says what went wrong twice: once in
the log for a human, and once as an ``::error::`` annotation for anything reading the run over the
API. Exit code is the command's own.
"""
import subprocess
import sys

MostCharacters = 1800
Telling = ('error', 'Error', 'ERROR', 'fatal', 'undefined reference', 'FAIL', 'Traceback',
           'assert', 'not found', 'No such file', 'cannot find', 'required')


def main() -> int:
    if '--' not in sys.argv[1:]:
        print(__doc__)
        return 2
    Split = sys.argv.index('--')
    Label = ' '.join(sys.argv[1:Split]) or 'command'
    Command = sys.argv[Split + 1:]
    if not Command:
        print(__doc__)
        return 2

    Spoken = []
    Running = subprocess.Popen(Command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors='replace', bufsize=1)
    assert Running.stdout is not None
    for Line in Running.stdout:
        sys.stdout.write(Line)
        sys.stdout.flush()
        Spoken.append(Line)
    Code = Running.wait()
    if Code == 0:
        return 0

    # GitHub caps an annotation, so lead with the lines that carry the diagnosis and follow with
    #    the last of the output; a bare tail is usually the quiet part after the failure.
    Lines = [Line.rstrip() for Line in Spoken if Line.strip()]
    Diagnosing = [Line for Line in Lines if any(Word in Line for Word in Telling)][:18]
    Closing = Lines[-14:]
    Tail = '\n'.join(Diagnosing + ['--- the last of it ---'] + Closing)[-MostCharacters:]
    Escaped = Tail.replace('%', '%25').replace('\r', '').replace('\n', '%0A')
    print(f'::error title={Label} failed (exit {Code})::{Escaped}')
    return Code


if __name__ == '__main__':
    raise SystemExit(main())
