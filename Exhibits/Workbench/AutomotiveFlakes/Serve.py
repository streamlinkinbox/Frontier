#!/usr/bin/env python3
"""Serve the live paint lab without retaining old HTML or shader assets."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

class FreshAssets(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, max-age=0')
        super().end_headers()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=5188)
    args = parser.parse_args()
    gallery = Path(__file__).resolve().parents[2] / 'Gallery/AutomotiveFlakes'
    ThreadingHTTPServer(('0.0.0.0', args.port), partial(FreshAssets, directory=str(gallery))).serve_forever()
