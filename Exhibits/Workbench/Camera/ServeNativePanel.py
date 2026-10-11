#!/usr/bin/env python3
"""Read-only native captures, not a browser simulation of the editor."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[2]/'Gallery'/'CameraNative'
class Viewer(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(ROOT),**kwargs)
    def end_headers(self):self.send_header('Cache-Control','no-store');super().end_headers()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=5186);a=p.parse_args()
    print(f'Native Camera captures on 0.0.0.0:{a.port}',flush=True)
    ThreadingHTTPServer(('0.0.0.0',a.port),Viewer).serve_forever()
