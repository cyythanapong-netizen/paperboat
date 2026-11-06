#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Rebuilding and starting app..."
"$SCRIPT_DIR/rebuild_app.sh"
exec "$SCRIPT_DIR/start_app.sh" "$@"
