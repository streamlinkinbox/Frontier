"""Serve the WebGPU volume at /; keep both earlier HTML experiments accessible."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


class GPUHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_GET(self):
        if urlsplit(self.path).path == '/':
            self.path = '/gpu-fluid.html'
        super().do_GET()

    def do_HEAD(self):
        if urlsplit(self.path).path == '/':
            self.path = '/gpu-fluid.html'
        super().do_HEAD()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8004)
    args = parser.parse_args()
    handler = functools.partial(GPUHandler, directory=str(Path(__file__).resolve().parent))
    print(f'GPU volume on port {args.port}', flush=True)
    ThreadingHTTPServer(('0.0.0.0', args.port), handler).serve_forever()
