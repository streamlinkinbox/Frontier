"""No-cache preview, with a confined route to the separately preserved reference apps."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import time
import uuid
import os
import subprocess
from threading import Lock
from urllib.parse import unquote, urlsplit

REFERENCE_ROOT = Path(__file__).resolve().parents[3] / 'ReferenceApps'

class CsvExports:
    """Bounded, ephemeral exports only; never written into the checkout."""
    def __init__(self):
        self.items = {}
        self.lock = Lock()

    def _expire(self):
        now = time.monotonic()
        self.items = {k: v for k, v in self.items.items() if v[0] > now}

    def put(self, data):
        with self.lock:
            self._expire()
            while self.items and (len(self.items) >= 8 or sum(len(v[1]) for v in self.items.values()) + len(data) > 128 * 1024 * 1024):
                del self.items[next(iter(self.items))]
            token = uuid.uuid4().hex
            self.items[token] = (time.monotonic() + 600, data)
            return token

    def get(self, token):
        with self.lock:
            self._expire()
            value = self.items.get(token)
            return value[1] if value else None

EXPORTS = CsvExports()

class Handler(SimpleHTTPRequestHandler):
    def do_POST(self):
        if urlsplit(self.path).path != '/api/telemetry/exports':
            self.send_error(404)
            return
        if self.headers.get('Sec-Fetch-Site') == 'cross-site' or self.headers.get('Content-Type', '').split(';')[0] != 'text/csv':
            self.send_error(403)
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            length = 0
        if not 0 < length <= 64 * 1024 * 1024:
            self.send_error(413, 'CSV must be between 1 byte and 64 MiB')
            return
        data = self.rfile.read(length)
        if len(data) != length:
            self.send_error(400, 'Incomplete CSV')
            return
        token = EXPORTS.put(b'\xef\xbb\xbf' + data)
        body = json.dumps({'url': '/api/telemetry/exports/' + token + '.csv'}).encode()
        self.send_response(201)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        route = urlsplit(self.path).path
        if route.startswith('/api/telemetry/exports/'):
            name = route.rsplit('/', 1)[-1]
            token = name[:-4] if name.endswith('.csv') else ''
            if len(token) != 32 or any(c not in '0123456789abcdef' for c in token):
                self.send_error(404)
                return
            data = EXPORTS.get(token)
            if data is None:
                self.send_error(410, 'Export expired; click Download CSV again')
                return
            self.send_response(200)
            self.send_header('Content-Type', 'text/csv; charset=utf-8')
            self.send_header('Content-Disposition', 'attachment; filename="frontier-session.csv"')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(data)
            return
        super().do_GET()

    def translate_path(self, path):
        route = unquote(urlsplit(path).path)
        prefix = next((p for p in ('/reference-apps/', '/ReferenceApps/') if route.startswith(p)), None)
        if prefix:
            target = (REFERENCE_ROOT / route[len(prefix):]).resolve()
            if target.is_relative_to(REFERENCE_ROOT):
                return str(target)
            return str(REFERENCE_ROOT / '__invalid_path__')
        return super().translate_path(path)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()

def ensure_runtime_dependencies():
    """Snapshots omit node_modules; restore the locked browser runtime before serving."""
    root = Path(__file__).resolve().parents[1]
    jolt = root / 'node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js'
    if not jolt.is_file():
        print('Restoring preview runtime dependencies (Jolt physics)…', flush=True)
        subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=root, check=True)
    if not jolt.is_file():
        raise RuntimeError('Jolt physics module is missing after npm ci; preview cannot start.')
    return root


if __name__ == '__main__':
    os.chdir(ensure_runtime_dependencies())
    print('Frontier preview ready on 0.0.0.0:8080', flush=True)
    ThreadingHTTPServer(('0.0.0.0', 8080), Handler).serve_forever()
