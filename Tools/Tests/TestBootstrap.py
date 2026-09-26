#!/usr/bin/env python3
import importlib.util, io, json, tarfile, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
import urllib.error
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
    def fixture(self, root):
        source=self.archive(root)
        package={'name':'test','revision':'abc','sha256':b.digest(source),'witness':'source.h','url':'https://example.invalid/archive'}
        cache=root/'shared-cache';cache.mkdir()
        (cache/'test-abc.tar.gz').write_bytes(source.read_bytes())
        return package,cache
    def test_cached_offline_install_and_ready_without_cache(self):
        with tempfile.TemporaryDirectory() as d, patch.object(b,'download',side_effect=AssertionError('network')):
            root=Path(d);p,c=self.fixture(root)
            b.install(p,root,offline=True,cache_dir=c)
            (c/'test-abc.tar.gz').unlink()
            b.install(p,root,check=True)
            b.install(p,root,offline=True)
    def test_offline_cache_miss_never_downloads(self):
        with tempfile.TemporaryDirectory() as d, patch.object(b,'download',side_effect=AssertionError('network')):
            root=Path(d);p,c=self.fixture(root)
            with self.assertRaisesRegex(RuntimeError,'offline cache miss'):b.install(p,root,offline=True)
            self.assertFalse((root/'ExternalPackages/test').exists())
    def test_repair_preserves_backup_without_network(self):
        with tempfile.TemporaryDirectory() as d, patch.object(b,'download',side_effect=AssertionError('network')):
            root=Path(d);p,c=self.fixture(root);b.install(p,root,offline=True,cache_dir=c)
            dest=root/'ExternalPackages/test';(dest/'source.h').unlink();(dest/'custom').write_text('keep')
            b.install(p,root,offline=True,repair=True,cache_dir=c)
            self.assertEqual((dest/'source.h').read_bytes(),b'test')
            self.assertEqual(next((root/'ExternalPackages/.frontier-backups').glob('*/custom')).read_text(),'keep')
    def test_failed_repair_keeps_installed_tree(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p,c=self.fixture(root);b.install(p,root,offline=True,cache_dir=c)
            dest=root/'ExternalPackages/test';(dest/'custom').write_text('keep')
            (c/'test-abc.tar.gz').write_bytes(b'corrupt')
            with self.assertRaisesRegex(RuntimeError,'checksum'):b.install(p,root,repair=True,cache_dir=c)
            self.assertEqual((dest/'custom').read_text(),'keep')
            self.assertEqual((dest/'source.h').read_bytes(),b'test')
    def test_check_and_malformed_marker_never_download(self):
        with tempfile.TemporaryDirectory() as d, patch.object(b,'download',side_effect=AssertionError('network')):
            root=Path(d);p,c=self.fixture(root);b.install(p,root,offline=True,cache_dir=c)
            (root/'ExternalPackages/test/.frontier-dependency.json').write_text('{broken')
            with self.assertRaisesRegex(RuntimeError,'No download attempted'):b.install(p,root,check=True)
            b.install(p,root,repair=True,offline=True,cache_dir=c)
    def test_wrong_pin_check_preserves_files(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p,c=self.fixture(root);b.install(p,root,offline=True,cache_dir=c)
            p['revision']='changed'
            with self.assertRaisesRegex(RuntimeError,'wrong pin'):b.install(p,root,check=True)
            self.assertEqual((root/'ExternalPackages/test/source.h').read_bytes(),b'test')
    def test_curl_fallback_checks_hash_and_keeps_tls(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p,c=self.fixture(root);target=root/'partial'
            def curl(command,**kwargs):
                self.assertNotIn('--insecure',command);self.assertNotIn('-k',command)
                self.assertIn('--proto-redir',command)
                Path(command[command.index('--output')+1]).write_bytes((c/'test-abc.tar.gz').read_bytes())
            with patch.object(b,'open_https',side_effect=urllib.error.URLError('TLS')),patch.object(b.shutil,'which',return_value='curl'),patch.object(b.subprocess,'run',side_effect=curl) as run:
                b.download(p,target,attempts=1)
                self.assertEqual(b.digest(target),p['sha256']);self.assertEqual(run.call_count,1)
    def test_bad_download_checksum_no_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p,c=self.fixture(root);target=root/'partial'
            with patch.object(b,'open_https',return_value=io.BytesIO(b'wrong')),patch.object(b.subprocess,'run',side_effect=AssertionError('fallback')):
                with self.assertRaisesRegex(RuntimeError,'checksum'):b.download(p,target,attempts=1)
            self.assertFalse(target.exists())
    def test_http_redirect_refused(self):
        with self.assertRaisesRegex(RuntimeError,'non-HTTPS'):
            b.HTTPSRedirect().redirect_request(None,None,302,'',{},'http://example.invalid/archive')
    def test_lock_immutable(self):
        packages=json.loads((ROOT/'ExternalPackages/Dependencies.lock.json').read_text())['packages']
        self.assertEqual(len(packages),14)
        for p in packages:
            self.assertRegex(p['revision'],r'^[0-9a-f]{40}$');self.assertRegex(p['sha256'],r'^[0-9a-f]{64}$')
            self.assertTrue(p['url'].startswith('https://codeload.github.com/'))
if __name__=='__main__':unittest.main()
