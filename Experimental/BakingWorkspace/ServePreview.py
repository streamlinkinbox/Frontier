#!/usr/bin/env python3
"""Lightweight static preview server for Experimental/BakingWorkspace (binds 0.0.0.0, serves ../Project browser/Assets/)."""

import http.server
import os
import posixpath
import sys
import urllib.parse

EXP_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BAKE_DIR = os.path.join(EXP_ROOT, "BakingWorkspace")


class BakingPreviewHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path: str) -> str:
        parsed = urllib.parse.unquote(urllib.parse.urlsplit(path).path)
        clean = posixpath.normpath(parsed).lstrip("/")
        if clean in ("", "."):
            return os.path.join(BAKE_DIR, "index.html")
        candidate_bake = os.path.abspath(os.path.join(BAKE_DIR, clean))
        if candidate_bake.startswith(EXP_ROOT) and os.path.exists(candidate_bake):
            return candidate_bake
        candidate_exp = os.path.abspath(os.path.join(EXP_ROOT, clean))
        if candidate_exp.startswith(EXP_ROOT) and os.path.exists(candidate_exp):
            return candidate_exp
        return candidate_bake

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 4173
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), BakingPreviewHandler)
    print(f"Frontier Baking Workspace listening on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
