#!/usr/bin/env python3
"""
trpm-proxy — bridges Toriginal OS (plain HTTP only, no TLS) to the real
HTTPS-only GitHub package repo.

Run this on a machine with real internet access (your dev machine, a
VPS, whatever). Toriginal OS's `trpm repo <this machine's IP>` points
at it; `trpm install <name>` then reaches your real GitHub repo through
this proxy.

What it does, for real:
  1. Listens on plain HTTP (default 0.0.0.0:8080 — reachable from your
     VM's network, e.g. VirtualBox NAT gateway 10.0.2.2 pointing back
     at your host, or a host-only adapter IP).
  2. On GET /<name>.trp, fetches the real file over HTTPS from
     https://raw.githubusercontent.com/warqwert1212/Toriginal-OS-pakage-repo/<branch>/<name>.trp
     using Python's real TLS (urllib + the system cert store — this is
     genuine HTTPS, not a workaround around it).
  3. Caches every successful fetch to ~/trpm-proxy-cache/<name>.trp —
     so repeated installs of the same package don't re-hit GitHub, and
     you have a visible local copy of everything that's been fetched.
  4. Serves the bytes back over plain HTTP with a correct
     Content-Length — exactly what Toriginal OS's http.c expects.

Usage:
  python3 trpm-proxy.py [--port 8080] [--branch main]
                        [--repo warqwert1212/Toriginal-OS-pakage-repo]
"""
import argparse
import os
import sys
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CACHE_DIR = os.path.expanduser("~/trpm-proxy-cache")


def make_handler(upstream_base: str):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stderr.write(f"[trpm-proxy] {self.address_string()} - {fmt % args}\n")

        def do_GET(self):
            path = self.path.lstrip("/")
            if not path or "/" in path or ".." in path:
                self.send_error(400, "bad request path")
                return
            if not path.endswith(".trp"):
                self.send_error(400, "only .trp requests are served")
                return

            cache_path = os.path.join(CACHE_DIR, path)
            if os.path.isfile(cache_path):
                self.log_message("cache hit: %s", path)
                self._serve_bytes(open(cache_path, "rb").read())
                return

            upstream_url = f"{upstream_base}/{path}"
            self.log_message("fetching from real repo: %s", upstream_url)
            try:
                with urllib.request.urlopen(upstream_url, timeout=15) as resp:
                    data = resp.read()
            except urllib.error.HTTPError as e:
                self.log_message("upstream returned %s for %s", e.code, path)
                self.send_error(e.code, f"upstream: {e.reason}")
                return
            except urllib.error.URLError as e:
                self.log_message("upstream fetch failed: %s", e.reason)
                self.send_error(502, f"upstream unreachable: {e.reason}")
                return

            os.makedirs(CACHE_DIR, exist_ok=True)
            with open(cache_path, "wb") as f:
                f.write(data)
            self.log_message("cached %s (%d bytes) -> %s", path, len(data), cache_path)

            self._serve_bytes(data)

        def _serve_bytes(self, data: bytes):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    return Handler


def main():
    p = argparse.ArgumentParser(description="Plain-HTTP proxy bridging Toriginal OS to the real HTTPS package repo.")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--repo", default="warqwert1212/Toriginal-OS-pakage-repo")
    p.add_argument("--branch", default="main")
    p.add_argument("--bind", default="0.0.0.0")
    args = p.parse_args()

    upstream_base = f"https://raw.githubusercontent.com/{args.repo}/{args.branch}"
    os.makedirs(CACHE_DIR, exist_ok=True)

    print(f"[trpm-proxy] upstream: {upstream_base}")
    print(f"[trpm-proxy] cache dir: {CACHE_DIR}")
    print(f"[trpm-proxy] listening on {args.bind}:{args.port}")
    print(f"[trpm-proxy] in Toriginal OS: trpm repo <this machine's IP> {args.port if args.port != 80 else ''}".rstrip())

    server = ThreadingHTTPServer((args.bind, args.port), make_handler(upstream_base))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[trpm-proxy] shutting down")


if __name__ == "__main__":
    main()
