#!/usr/bin/env python3
"""Read-only native proof viewer; does not simulate the ImGui application."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[2]/'Gallery'/'SunFullPanel'
class Viewer(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(ROOT),**kwargs)
    def end_headers(self):self.send_header('Cache-Control','no-store');super().end_headers()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=5177);a=p.parse_args()
    print(f'Full native Sun inspector captures on 0.0.0.0:{a.port}',flush=True)
    ThreadingHTTPServer(('0.0.0.0',a.port),Viewer).serve_forever()
