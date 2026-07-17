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
import hashlib
import hmac
import os
import sys
import urllib.request
import urllib.error
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CACHE_DIR = os.path.expanduser("~/trpm-proxy-cache")

# Must match TRP_HMAC_SECRET in root/freeNT/include/config.h on the OS
# side - the kernel refuses to install any package whose HMAC tag it
# can't verify against this same string. CHANGE THE DEFAULT before
# relying on this for anything; anyone who has this value can forge a
# valid tag for arbitrary package contents.
DEFAULT_SECRET = "CHANGE-ME-toriginal-trpm-shared-secret"


def make_handler(upstream_base: str, secret: bytes):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stderr.write(f"[trpm-proxy] {self.address_string()} - {fmt % args}\n")

        def do_GET(self):
            # FIX: decode percent-encoding *before* checking for "/" and
            # "..", so an encoded traversal sequence (e.g. "%2e%2e%2f")
            # can't slide past the literal-character checks below.
            raw_path = self.path.lstrip("/")
            path = urllib.parse.unquote(raw_path)
            if not path or "/" in path or ".." in path or "\\" in path:
                self.send_error(400, "bad request path")
                return
            if not path.endswith(".trp"):
                self.send_error(400, "only .trp requests are served")
                return

            cache_path = os.path.join(CACHE_DIR, path)
            # Defense in depth: even with the checks above, make sure the
            # resolved path is still actually inside CACHE_DIR before
            # touching the filesystem.
            if os.path.realpath(cache_path) != os.path.join(os.path.realpath(CACHE_DIR), path):
                self.send_error(400, "bad request path")
                return

            if os.path.isfile(cache_path):
                self.log_message("cache hit: %s", path)
                with open(cache_path, "rb") as f:
                    data = f.read()
                self._serve_bytes(data)
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
            tag = hmac.new(secret, data, hashlib.sha256).hexdigest()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("X-Trp-Hmac", tag)
            self.end_headers()
            self.wfile.write(data)

    return Handler


def main():
    p = argparse.ArgumentParser(description="Plain-HTTP proxy bridging Toriginal OS to the real HTTPS package repo.")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--repo", default="warqwert1212/Toriginal-OS-pakage-repo")
    p.add_argument("--branch", default="main")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--secret", default=os.environ.get("TRPM_HMAC_SECRET", DEFAULT_SECRET),
                    help="shared secret for X-Trp-Hmac tagging; must match TRP_HMAC_SECRET "
                         "in config.h on the OS side. Also settable via TRPM_HMAC_SECRET env var.")
    args = p.parse_args()

    upstream_base = f"https://raw.githubusercontent.com/{args.repo}/{args.branch}"
    os.makedirs(CACHE_DIR, exist_ok=True)

    if args.secret == DEFAULT_SECRET:
        print("[trpm-proxy] WARNING: using the placeholder default secret - anyone who has read")
        print("[trpm-proxy]          this script can forge valid X-Trp-Hmac tags. Set --secret or")
        print("[trpm-proxy]          TRPM_HMAC_SECRET to something private, and update the matching")
        print("[trpm-proxy]          TRP_HMAC_SECRET in config.h to the same value.")

    print(f"[trpm-proxy] upstream: {upstream_base}")
    print(f"[trpm-proxy] cache dir: {CACHE_DIR}")
    print(f"[trpm-proxy] listening on {args.bind}:{args.port}")
    print(f"[trpm-proxy] in Toriginal OS: trpm repo <this machine's IP> {args.port if args.port != 80 else ''}".rstrip())

    server = ThreadingHTTPServer((args.bind, args.port), make_handler(upstream_base, args.secret.encode("utf-8")))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[trpm-proxy] shutting down")


if __name__ == "__main__":
    main()
