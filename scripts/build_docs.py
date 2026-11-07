#!/usr/bin/env python3
"""Build Paperboat Sphinx documentation (docs/api -> docs/api/_build).

Usage:
    python3 scripts/build_docs.py               # Build docs with warnings as errors
    python3 scripts/build_docs.py --clean       # Rebuild all sources from scratch
    python3 scripts/build_docs.py --open        # Open in default web browser after build
    python3 scripts/build_docs.py --serve       # Serve docs on a local HTTP port
    python3 scripts/build_docs.py --port 8080   # Custom HTTP server port
"""

import argparse
import http.server
import os
from pathlib import Path
import shutil
import socketserver
import subprocess
import sys
import webbrowser

ROOT = Path(__file__).resolve().parents[1]
DOCS_DIR = ROOT / "docs" / "api"
BUILD_DIR = DOCS_DIR / "_build"
GEN_TOKENS = ROOT / "docs" / "gen_tokens.py"


def check_dependencies() -> None:
    """Ensure shibuya theme is installed."""
    try:
        import shibuya  # noqa: F401
    except ImportError:
        print("[deps] Installing shibuya Sphinx theme...")
        subprocess.run([sys.executable, "-m", "pip", "install", "shibuya"], check=True)


def sync_tokens() -> None:
    """Ensure docs/_static/tokens.css is synchronized with packages/shared/src/tokens.css."""
    check_dependencies()
    if not GEN_TOKENS.exists():
        return
    res = subprocess.run([sys.executable, str(GEN_TOKENS)], cwd=ROOT, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(f"Failed to generate tokens: {res.stderr}\n")
        sys.exit(res.returncode)
    if res.stdout.strip():
        print(f"[tokens] {res.stdout.strip()}")


def build_docs(clean: bool = False, extra_args: list[str] | None = None) -> int:
    """Run sphinx-build."""
    sync_tokens()

    if clean and BUILD_DIR.exists():
        print(f"[clean] Removing {BUILD_DIR.relative_to(ROOT)}...")
        shutil.rmtree(BUILD_DIR)

    cmd = [
        "sphinx-build",
        "-W",  # Treat warnings as errors
        "-b", "html",
    ]
    if clean:
        cmd.extend(["-a", "-E"])

    cmd.extend([str(DOCS_DIR), str(BUILD_DIR)])
    if extra_args:
        cmd.extend(extra_args)

    print(f"[build] Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode == 0:
        index_path = BUILD_DIR / "index.html"
        print(f"\nDocumentation built successfully:")
        print(f"  file://{index_path.resolve()}\n")
    return result.returncode


def serve_docs(port: int = 8000) -> None:
    """Serve the built documentation over HTTP."""
    if not (BUILD_DIR / "index.html").exists():
        sys.exit(f"Error: Build directory {BUILD_DIR} does not exist. Run build first.")

    os.chdir(BUILD_DIR)
    handler = http.server.SimpleHTTPRequestHandler
    with socketserver.TCPServer(("", port), handler) as httpd:
        url = f"http://localhost:{port}/index.html"
        print(f"[serve] Serving documentation at {url}")
        print("Press Ctrl+C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped server.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and serve Paperboat documentation.")
    parser.add_argument("-c", "--clean", action="store_true", help="Rebuild all files from scratch")
    parser.add_argument("-o", "--open", action="store_true", help="Open docs in browser after successful build")
    parser.add_argument("-s", "--serve", action="store_true", help="Start local HTTP server after build")
    parser.add_argument("-p", "--port", type=int, default=8000, help="Port for local HTTP server (default: 8000)")

    args, unknown = parser.parse_known_args()

    rc = build_docs(clean=args.clean, extra_args=unknown)
    if rc != 0:
        sys.exit(rc)

    index_url = f"file://{(BUILD_DIR / 'index.html').resolve()}"
    if args.open:
        webbrowser.open(index_url)

    if args.serve:
        serve_docs(port=args.port)


if __name__ == "__main__":
    main()
