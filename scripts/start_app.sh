#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_PORT="${PORT:-3047}"

# Kill any existing process listening on the app port
if command -v fuser >/dev/null 2>&1; then
  fuser -k "${APP_PORT}/tcp" 2>/dev/null || true
fi

# Stop any running systemd user service if present
if command -v systemctl >/dev/null 2>&1; then
  systemctl --user stop paperboat-web.service 2>/dev/null || true
fi

echo "==> Starting web application in dev mode on port ${APP_PORT}..."
cd "$REPO_ROOT"
npm run dev -- --host 0.0.0.0 --port "${APP_PORT}"
