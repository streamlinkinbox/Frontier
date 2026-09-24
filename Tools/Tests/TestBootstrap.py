#!/usr/bin/env python3
import importlib.util, io, json, tarfile, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('bootstrap',ROOT/'Tools/Bootstrap.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
class BootstrapTests(unittest.TestCase):
    def archive(self, root, name='owner/source.h', link=False):
        path=root/'input.tar.gz'
        with tarfile.open(path,'w:gz') as t:
            m=tarfile.TarInfo(name)
            if link:m.type=tarfile.SYMTYPE;m.linkname='/etc/passwd';t.addfile(m)
            else:m.size=4;t.addfile(m,io.BytesIO(b'test'))
        return path
    def test_strip_prefix(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);b.extract(self.archive(root),root/'out')
            self.assertEqual((root/'out/source.h').read_bytes(),b'test')
    def test_traversal_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            with self.assertRaises(RuntimeError):b.extract(self.archive(root,'owner/../../escape'),root/'out')
    def test_link_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            with self.assertRaises(RuntimeError):b.extract(self.archive(root,link=True),root/'out')
    def test_existing_directory_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);dest=root/'ExternalPackages/test';dest.mkdir(parents=True);(dest/'mine').write_text('keep')
            with self.assertRaises(RuntimeError):b.install({'name':'test','revision':'abc','sha256':'abc'},root)
            self.assertEqual((dest/'mine').read_text(),'keep')
    def test_checksum_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);cache=root/'.cache/dependency-archives';cache.mkdir(parents=True);(cache/'test-abc.tar.gz').write_bytes(b'bad')
            with self.assertRaises(RuntimeError):b.install({'name':'test','revision':'abc','sha256':'abc'},root)
            self.assertFalse((root/'ExternalPackages/test').exists())
    def test_lock_immutable(self):
        packages=json.loads((ROOT/'ExternalPackages/Dependencies.lock.json').read_text())['packages']
        self.assertEqual(len(packages),14)
        for p in packages:
            self.assertRegex(p['revision'],r'^[0-9a-f]{40}$');self.assertRegex(p['sha256'],r'^[0-9a-f]{64}$')
            self.assertTrue(p['url'].startswith('https://codeload.github.com/'))
if __name__=='__main__':unittest.main()
