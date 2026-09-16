#!/usr/bin/env python3
"""Cache-proof static server for the ocean simulator.

ES modules are cached aggressively by browsers; stale JS while iterating
looks exactly like 'my fix did nothing'. This server sends no-store on
everything so a plain refresh always runs the current code.
"""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import sys


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print(f'ocean-sim on http://localhost:{port} (no-cache)', flush=True)
    ThreadingHTTPServer(('0.0.0.0', port), NoCacheHandler).serve_forever()
