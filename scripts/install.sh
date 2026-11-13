#!/bin/sh
# Install or update paperboat for this user, with nothing but this script:
#
#   curl -fsSL https://raw.githubusercontent.com/paperboatlab/paperboat/HEAD/scripts/install.sh | sh
#   curl -fsSL …/install.sh | sh -s -- --main      # build main from source (toolkits work)
#
#   (no flag)    release: the newest GitHub release's prebuilt .tar.gz (prereleases included)
#   --source     release, built from source at its tag: toolkits work (they compile into the engine)
#   --main       the default branch (main or dev), built from source
#   --version V  that release instead of the newest (with or without --source)
#   --check      say what is installed and what is available, change nothing
#
# Running it again updates along the same channel; a flag switches channel. `paperboat update` runs it.
# Layout: $PAPERBOAT_HOME (default ~/.local/share/paperboat) holds self/<version>/ (prebuilt),
# src/paperboat (source) and apps/ (`paperboat pkg`); ~/.local/bin/paperboat links to the one in use.
set -eu

REPO=paperboatlab/paperboat
HOME_DIR=${PAPERBOAT_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/paperboat}
BIN_DIR=${PAPERBOAT_BIN:-$HOME/.local/bin}
SRC=$HOME_DIR/src/paperboat
LINK=$BIN_DIR/paperboat

mode="" version="" check=0
while [ $# -gt 0 ]; do
  case $1 in
    --source) mode=source ;;
    --main) mode=main ;;
    --release) mode=release ;;
    --version) version=${2#v}; shift ;;
    --check) check=1 ;;
    -h|--help) sed -n '2,16p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "install.sh: unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

say() { printf '%s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "needs $1${2:+ ($2)}"; }
fetch() { if command -v curl >/dev/null 2>&1; then curl -fsSL "$@"; else need wget "or curl"; wget -qO- "$1"; fi; }

# What is installed now: the channel from where the link points (main | source | release | "").
current() {
  target=$(readlink "$LINK" 2>/dev/null || true)
  case $target in
    "$SRC"/*)
      if git -C "$SRC" symbolic-ref -q HEAD >/dev/null; then echo main; else echo source; fi ;;
    "$HOME_DIR"/self/*) echo release ;;
  esac
}
installed_version() {
  case $(current) in
    main) echo "main@$(git -C "$SRC" rev-parse --short HEAD)" ;;
    source) git -C "$SRC" describe --tags --exact-match 2>/dev/null | sed 's/^v//' ;;
    release) basename "$(dirname "$(dirname "$(readlink "$LINK")")")" ;;
  esac
}
# The newest release's tag without the v (GitHub lists the newest first, prereleases too).
latest() {
  fetch "https://api.github.com/repos/$REPO/releases?per_page=1" |
    sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' | head -n1
}

was=$(current)
[ -n "$mode" ] || mode=${was:-release}

if [ "$mode" = main ]; then
  want=main
else
  want=${version:-$(latest)}
  [ -n "$want" ] || die "cannot read the releases of $REPO (offline, or rate limited)"
fi
have=$(installed_version)

if [ $check -eq 1 ]; then
  say "installed: ${have:-nothing} (${was:-no channel})"
  if [ "$mode" = main ] && [ "$was" = main ]; then
    git -C "$SRC" fetch -q origin
    behind=$(git -C "$SRC" rev-list --count HEAD..@{upstream})
    say "main: $behind commit(s) behind $(git -C "$SRC" rev-parse --abbrev-ref @{upstream})"
  else
    say "available: $want ($mode)"
  fi
  exit 0
fi

link() {
  mkdir -p "$BIN_DIR"
  ln -sfn "$1" "$LINK"
  say "paperboat → $1"
  case ":$PATH:" in *":$BIN_DIR:"*) ;; *) say "add $BIN_DIR to PATH: export PATH=\"$BIN_DIR:\$PATH\"" ;; esac
}

if [ "$mode" = release ]; then
  if [ "$was" = release ] && [ "$have" = "$want" ]; then say "paperboat $want is up to date"; exit 0; fi
  asset="paperboat-$want-$(uname -s)-$(uname -m).tar.gz"
  dest=$HOME_DIR/self/$want
  say "downloading $asset…"
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  fetch "https://github.com/$REPO/releases/download/v$want/$asset" > "$tmp/pb.tar.gz" ||
    die "no $asset in release v$want (this platform may need --source)"
  mkdir -p "$tmp/x"
  tar xzf "$tmp/pb.tar.gz" -C "$tmp/x" --strip-components=1
  rm -rf "$dest"
  mkdir -p "$HOME_DIR/self"
  mv "$tmp/x" "$dest"
  link "$dest/bin/paperboat"
  # ponytail: older versions stay in self/ (a rollback is `ln -sfn`); prune by hand if disk matters.
  say "installed paperboat $want"
  exit 0
fi

# Source: one checkout, on main or detached at a release tag.
need git; need cmake; need npm "Node.js 22+"
if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$(dirname "$SRC")"
  git clone -q "https://github.com/$REPO.git" "$SRC"
fi
if [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]; then
  die "$SRC has local changes; commit or stash them first"
fi
before=$(git -C "$SRC" rev-parse HEAD)
git -C "$SRC" fetch -q --tags origin
if [ "$mode" = main ]; then
  branch=$(git -C "$SRC" symbolic-ref --short refs/remotes/origin/HEAD | sed 's|^origin/||')
  git -C "$SRC" checkout -q "$branch"
  git -C "$SRC" merge -q --ff-only "origin/$branch"
else
  git -C "$SRC" checkout -q "v$want"
fi
after=$(git -C "$SRC" rev-parse HEAD)
if [ "$before" = "$after" ] && [ -x "$SRC/engine/build/tools/paperboat" ] && [ "$was" = "$mode" ]; then
  say "paperboat $(installed_version) is up to date"
  exit 0
fi
say "building paperboat $want from source in $SRC…"
# The web app builds against the sibling paperboat-shared (file:../../../paperboat-shared), at its
# default branch as the release workflow does.
SHARED=$(dirname "$SRC")/paperboat-shared
if [ -d "$SHARED/.git" ]; then git -C "$SHARED" pull -q --ff-only
else git clone -q "https://github.com/paperboatlab/paperboat-shared.git" "$SHARED"; fi
(cd "$SHARED" && npm ci --no-audit --no-fund)
cd "$SRC"
npm ci --no-audit --no-fund
npm run build
gen=""; command -v ninja >/dev/null 2>&1 && gen="-G Ninja"
[ -f engine/build/CMakeCache.txt ] && gen=""   # keep the generator it was configured with
# shellcheck disable=SC2086
cmake -S engine -B engine/build $gen -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --parallel
link "$SRC/bin/paperboat"
say "installed paperboat $want (source)"
