#!/usr/bin/env python3
"""Read-only viewer for CPU-generated proof artifacts, not a replacement UI."""
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import argparse
ROOT=Path(__file__).resolve().parents[2]/'Gallery'
class ProofRequest(SimpleHTTPRequestHandler):
    start_page = "/IconArt/index.html"
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(ROOT),**kwargs)
    def end_headers(self):self.send_header('Cache-Control','no-store');super().end_headers()
    def do_GET(self):
        if urlsplit(self.path).path=='/':self.path=self.start_page
        super().do_GET()
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=5176);p.add_argument('--page',choices=['IconArt','NativeOutliner'],default='IconArt');a=p.parse_args()
    ProofRequest.start_page='/'+a.page+'/index.html'
    print(f'C++ CPU proof viewer on 0.0.0.0:{a.port}',flush=True)
    ThreadingHTTPServer(('0.0.0.0',a.port),ProofRequest).serve_forever()
