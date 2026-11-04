#============================================================================================================================================
#                                                   WINDOWSDISTRIBUTIONVALIDATION.PY
#============================================================================================================================================
# 🧩 Verify that incomplete safety results cannot produce a downloadable executable archive.

from pathlib import Path
import json
import importlib.util
import subprocess
import sys
import tempfile

Repository = Path(__file__).resolve().parents[3]
Scratch = Repository / '_ClaudeScratch/tmp'
Scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=Scratch, prefix='distribution-refusal-') as Temporary:
    Directory = Path(Temporary)
    for Index, Report in enumerate([
        {'distribution_ready': False},
        {'distribution_ready': True, 'defender': {'clean': False}},
        {'distribution_ready': True, 'defender': {'clean': True, 'status': 'passed'}, 'signatures': []},
        {'distribution_ready': True, 'defender': {'clean': True, 'status': 'passed'}, 'signatures': [
            {'application': Name, 'status': 'NotSigned'} for Name in ('Frontier', 'TextureWorkspace')]},
    ]):
        Source = Directory / ('Refusal' + str(Index) + '.json')
        Source.write_text(json.dumps(Report), encoding='utf-8')
        Archive = Directory / ('Refusal' + str(Index) + '.zip')
        Execution = subprocess.run([sys.executable, str(Repository / 'Automation/PackageWorkspacesWindows.py'),
            '--build', str(Directory), '--security-report', str(Source), '--output', str(Archive)],
            capture_output=True, text=True)
        assert Execution.returncode != 0 and not Archive.exists(), 'Incomplete safety results produced an archive'
        assert 'Distribution withheld' in Execution.stderr, 'Refusal did not explain its security boundary'
print('PASS: four incomplete/untrusted distribution reports refused; no executable archives produced.')

Specification = importlib.util.spec_from_file_location('WindowsPackageAuthorization', Repository / 'Automation/PackageWorkspacesWindows.py')
Authorization = importlib.util.module_from_spec(Specification)
Specification.loader.exec_module(Authorization)
Count = 0
for UnsignedEnabled in (False, True):
    for UnsignedAuthorized in (False, True):
        for SignatureStatus in ('Valid', 'NotSigned', 'HashMismatch', 'NotTrusted', 'UnknownError', 'NotSupportedFileFormat'):
            for ScanStatus in ('passed', 'failed', 'unavailable'):
                Report = {
                    'distribution_ready': True,
                    'unsigned_preview_authorized': UnsignedAuthorized,
                    'defender': {'clean': ScanStatus == 'passed', 'status': ScanStatus},
                    'signatures': [{'application': Name, 'status': SignatureStatus}
                                   for Name in ('Frontier', 'TextureWorkspace')],
                }
                Expected = ScanStatus == 'passed' and (SignatureStatus == 'Valid' or
                    (SignatureStatus == 'NotSigned' and UnsignedEnabled and UnsignedAuthorized))
                try:
                    Authorization.VerifyDistributionAuthorization(Report, UnsignedEnabled)
                    Accepted = True
                except RuntimeError:
                    Accepted = False
                assert Accepted == Expected, 'Signature/scan authorization boundary differs'
                Count += 1
print('PASS:', Count, 'signature/scan policy combinations; unsigned needs both explicit authorizations.')
