#!/usr/bin/env bash
# Build the API docs with warnings treated as errors, the same way CI does.
# Usage: scripts/test-docs.sh [clean]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Sync design tokens + fonts from the app (one source).
"$ROOT/scripts/sync-doc-assets.sh"

if [ "${1:-}" = "clean" ]; then
  rm -rf docs/api/_build
fi

echo "Building docs (sphinx-build -W)..."
sphinx-build -W -b html docs/api docs/api/_build
echo "Docs build OK."
