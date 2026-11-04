"""Verify CAD companion selection using synthetic PE headers, not executable product binaries."""
from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest

RepositoryRoot = Path(__file__).resolve().parents[4]
Specification = importlib.util.spec_from_file_location(
    'WindowsPreviewPackaging', RepositoryRoot / 'Automation/PackageEditorPreviewWindows.py')
Packaging = importlib.util.module_from_spec(Specification)
Specification.loader.exec_module(Packaging)


def WriteExecutable(Pathname, Text=b'', Machine=0x8664):
    Payload = bytearray(256)
    Payload[:2] = b'MZ'
    struct.pack_into('<I', Payload, 0x3c, 64)
    Payload[64:68] = b'PE\0\0'
    struct.pack_into('<H', Payload, 68, Machine)
    struct.pack_into('<H', Payload, 88, 0x20b)
    Pathname.write_bytes(Payload + Text)


class CompanionSelectionValidation(unittest.TestCase):
    def setUp(self):
        self.Temporary = tempfile.TemporaryDirectory(prefix='SolidArc Windows package ')
        self.Directory = Path(self.Temporary.name)
        self.Frontier = self.Directory / 'Frontier.exe'
        self.Cad = self.Directory / 'SolidArcWorkspace.exe'
        WriteExecutable(self.Frontier)

    def tearDown(self):
        self.Temporary.cleanup()

    def test_non_cad_editor_stays_unchanged(self):
        self.assertIsNone(Packaging.LocateSolidArcExecutable(self.Frontier))

    def test_ascii_cad_launch_requires_companion(self):
        WriteExecutable(self.Frontier, b'SolidArcWorkspace.exe')
        with self.assertRaisesRegex(RuntimeError, 'requires SolidArcWorkspace'):
            Packaging.LocateSolidArcExecutable(self.Frontier)

    def test_wide_cad_launch_requires_companion(self):
        WriteExecutable(self.Frontier, 'SolidArcWorkspace.exe'.encode('utf-16-le'))
        with self.assertRaisesRegex(RuntimeError, 'requires SolidArcWorkspace'):
            Packaging.LocateSolidArcExecutable(self.Frontier)

    def test_current_companion_selected(self):
        WriteExecutable(self.Frontier, 'SolidArcWorkspace.exe'.encode('utf-16-le'))
        WriteExecutable(self.Cad, b'Shift straight Surface cutters')
        self.assertEqual(Packaging.LocateSolidArcExecutable(self.Frontier), self.Cad)

    def test_pre_shift_companion_refused(self):
        WriteExecutable(self.Cad, b'Surface cutters')
        with self.assertRaisesRegex(RuntimeError, 'stale'):
            Packaging.LocateSolidArcExecutable(self.Frontier, self.Cad)

    def test_pre_cutter_companion_refused(self):
        WriteExecutable(self.Cad, b'Shift straight')
        with self.assertRaisesRegex(RuntimeError, 'stale'):
            Packaging.LocateSolidArcExecutable(self.Frontier, self.Cad)

    def test_wrong_architecture_refused(self):
        WriteExecutable(self.Cad, b'Shift straight Surface cutters', Machine=0x14c)
        with self.assertRaisesRegex(RuntimeError, 'Windows x64'):
            Packaging.LocateSolidArcExecutable(self.Frontier, self.Cad)

    def test_explicit_spaced_path(self):
        Requested = self.Directory / 'CAD with spaces.exe'
        WriteExecutable(Requested, b'Shift straight Surface cutters')
        self.assertEqual(Packaging.LocateSolidArcExecutable(self.Frontier, Requested), Requested.resolve())

    def test_selection_preserves_source(self):
        WriteExecutable(self.Cad, b'Shift straight Surface cutters')
        Before = self.Cad.read_bytes()
        Packaging.LocateSolidArcExecutable(self.Frontier, self.Cad)
        self.assertEqual(self.Cad.read_bytes(), Before)

    # The New dialog's texture library starts TextureWorkspace.exe beside Frontier.exe.
    def test_editor_without_texture_launch_stays_unchanged(self):
        self.assertIsNone(Packaging.LocateTextureExecutable(self.Frontier))

    def test_texture_launch_requires_companion(self):
        WriteExecutable(self.Frontier, 'TextureWorkspace.exe'.encode('utf-16-le'))
        with self.assertRaisesRegex(RuntimeError, 'TextureWorkspace.exe'):
            Packaging.LocateTextureExecutable(self.Frontier)

    def test_texture_companion_selected(self):
        WriteExecutable(self.Frontier, 'TextureWorkspace.exe'.encode('utf-16-le'))
        Texture = self.Directory / 'TextureWorkspace.exe'
        WriteExecutable(Texture)
        self.assertEqual(Packaging.LocateTextureExecutable(self.Frontier), Texture)

    def test_texture_companion_wrong_architecture_refused(self):
        WriteExecutable(self.Frontier, 'TextureWorkspace.exe'.encode('utf-16-le'))
        WriteExecutable(self.Directory / 'TextureWorkspace.exe', Machine=0x14c)
        with self.assertRaisesRegex(RuntimeError, 'Windows x64'):
            Packaging.LocateTextureExecutable(self.Frontier)


if __name__ == '__main__':
    unittest.main(verbosity=2)
