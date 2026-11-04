#============================================================================================================================================
#                                                     FONTCATALOGUEVALIDATION.PY
#============================================================================================================================================
# 🧩 Validate offline inventory generation, stable output, deleted faces, and safe rejection of damaged fonts.

from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
try:
    import tomllib
except ImportError:
    import tomli as tomllib

RepositoryRoot = Path(__file__).resolve().parents[3]
(RepositoryRoot / '_ClaudeScratch/tmp').mkdir(parents=True, exist_ok=True)


class FontCatalogueValidation(unittest.TestCase):
    def Invoke(self, FontRoot, *Arguments):
        return subprocess.run([sys.executable, str(RepositoryRoot / 'Automation/PrepareFonts.py'),
                               '--root', str(FontRoot), *Arguments], capture_output=True, text=True)

    def test_offline_inventory_and_removal(self):
        with tempfile.TemporaryDirectory(dir=RepositoryRoot / '_ClaudeScratch/tmp') as Temporary:
            FontRoot = Path(Temporary)
            Family = FontRoot / 'LocalFonts'
            Family.mkdir()
            Source = RepositoryRoot / 'EngineContent/FontArchives/Archivo/Archivo-Regular.ttf'
            shutil.copy2(Source, Family / 'Archivo-Regular.ttf')
            Result = self.Invoke(FontRoot, '--scan-only')
            self.assertEqual(Result.returncode, 0, Result.stdout + Result.stderr)
            Catalogue = FontRoot / 'FontCatalogue.json'
            Initial = Catalogue.read_bytes()
            Document = json.loads(Initial)
            self.assertEqual(len(Document['faces']), 1)
            self.assertEqual(Document['faces'][0]['family'], 'Archivo')
            Runtime = tomllib.loads((FontRoot / 'FontCatalogue.toml').read_text())
            self.assertEqual(Runtime['faces'], Document['faces'])
            self.assertEqual(self.Invoke(FontRoot, '--scan-only').returncode, 0)
            self.assertEqual(Initial, Catalogue.read_bytes())
            (Family / 'Archivo-Regular.ttf').unlink()
            self.assertEqual(self.Invoke(FontRoot, '--scan-only').returncode, 0)
            self.assertEqual(json.loads(Catalogue.read_bytes())['faces'], [])
            self.assertNotIn('faces', tomllib.loads((FontRoot / 'FontCatalogue.toml').read_text()))

    def test_corrupt_font_is_not_silently_accepted(self):
        with tempfile.TemporaryDirectory(dir=RepositoryRoot / '_ClaudeScratch/tmp') as Temporary:
            FontRoot = Path(Temporary)
            (FontRoot / 'Broken.ttf').write_bytes(b'not a font')
            self.assertNotEqual(self.Invoke(FontRoot, '--scan-only').returncode, 0)
            self.assertEqual(len(json.loads((FontRoot / 'FontCatalogue.json').read_bytes())['rejected']), 1)

    def test_verify_preserves_modified_pinned_file(self):
        with tempfile.TemporaryDirectory(dir=RepositoryRoot / '_ClaudeScratch/tmp') as Temporary:
            FontRoot = Path(Temporary)
            Entry = json.loads((RepositoryRoot / 'Automation/FontSources.json').read_text())['files'][0]
            Destination = FontRoot / Entry['family'] / Entry['file']
            Destination.parent.mkdir()
            Destination.write_bytes(b'local edits')
            Result = self.Invoke(FontRoot, '--verify')
            self.assertNotEqual(Result.returncode, 0)
            self.assertIn('Modified pinned font', Result.stdout)
            self.assertEqual(Destination.read_bytes(), b'local edits')


if __name__ == '__main__':
    unittest.main()
