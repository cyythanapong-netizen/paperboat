#!/usr/bin/env bash
# Copy the app's design tokens and self-hosted fonts into the docs static dir,
# so the docs read the same source as the app (no CDN).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/docs/api/_static"
mkdir -p "$DEST/fonts"

# Tokens: one source is apps/web/src/tokens.css.
cp "$ROOT/apps/web/src/tokens.css" "$DEST/tokens.css"
# Drop the npm @import lines; fonts are self-hosted via @font-face in paperboat.css.
sed -i '/^@import "@fontsource/d' "$DEST/tokens.css"

# Self-hosted fonts (Geist + Geist Mono), the weights the docs use.
copyfont() {
  local family="$1" weight="$2"
  local src="$ROOT/node_modules/@fontsource/$family/files/${family}-latin-${weight}-normal.woff2"
  local dst="$DEST/fonts/${family}-latin-${weight}-normal.woff2"
  cp "$src" "$dst"
}
for w in 400 500 600; do copyfont geist-sans "$w"; done
for w in 400 500; do copyfont geist-mono "$w"; done
echo "Doc assets synced."
