"""Run: python3 -m unittest discover -s Tools -p 'test_csv_exports.py'."""
import importlib.util
import json
from pathlib import Path
import threading
import time
import unittest
from urllib.request import Request, urlopen
from urllib.error import HTTPError

spec = importlib.util.spec_from_file_location('preview', Path(__file__).with_name('Serve.py'))
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)

class QuietHandler(preview.Handler):
    def log_message(self, *args):
        pass

class CsvExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = preview.ThreadingHTTPServer(('127.0.0.1', 0), QuietHandler)
        cls.base = 'http://127.0.0.1:' + str(cls.server.server_port)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def test_attachment_bytes_headers_and_retry(self):
        data = '"message"\r\n"café, ""quote"""'.encode()
        req = Request(self.base + '/api/telemetry/exports', data=data, headers={'Content-Type': 'text/csv;charset=utf-8'})
        with urlopen(req) as response:
            self.assertEqual(response.status, 201)
            url = json.load(response)['url']
        for _ in range(2):
            with urlopen(self.base + url) as response:
                self.assertEqual(response.read(), b'\xef\xbb\xbf' + data)
                self.assertIn('attachment;', response.headers['Content-Disposition'])
                self.assertIn('no-store', response.headers['Cache-Control'])

    def test_invalid_and_expired_links(self):
        for path, code in [('/api/telemetry/exports/bad.csv', 404), ('/api/telemetry/exports/' + '0'*32 + '.csv', 410)]:
            with self.assertRaises(HTTPError) as error:
                urlopen(self.base + path)
            self.assertEqual(error.exception.code, code)

    def test_rejects_empty_wrong_type_and_cross_site(self):
        for data, headers, code in [(b'', {'Content-Type':'text/csv'}, 413), (b'a', {'Content-Type':'text/plain'}, 403), (b'a', {'Content-Type':'text/csv','Sec-Fetch-Site':'cross-site'}, 403)]:
            with self.assertRaises(HTTPError) as error:
                urlopen(Request(self.base + '/api/telemetry/exports', data=data, headers=headers))
            self.assertEqual(error.exception.code, code)

    def test_store_bounded_and_expires(self):
        store = preview.CsvExports()
        first = store.put(b'first')
        for _ in range(8):
            store.put(b'next')
        self.assertEqual(len(store.items), 8)
        self.assertIsNone(store.get(first))
        token = store.put(b'expired')
        store.items[token] = (time.monotonic()-1, b'expired')
        self.assertIsNone(store.get(token))

if __name__ == '__main__':
    unittest.main()
