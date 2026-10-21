#!/usr/bin/env python3
"""Static file server with caching disabled, for testing web builds
(plain http.server lets the browser reuse a stale dw.wasm).

Usage: serve_nocache.py [port] [root]"""
import http.server
import sys
from functools import partial


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
root = sys.argv[2] if len(sys.argv) > 2 else '.'
http.server.ThreadingHTTPServer(
    ('', port), partial(NoCache, directory=root)).serve_forever()
