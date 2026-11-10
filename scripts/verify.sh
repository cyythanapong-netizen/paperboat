#!/usr/bin/env bash
# Check paperboat and ../paperboat-shared (and ../squero-studio-om when it is checked out) and print one table.
#   scripts/verify.sh            everything: builds, engine tests, sdk-backend check, docs
#   scripts/verify.sh --quick    typecheck + unit tests + generator checks only (seconds)
# Exit 1 if any step fails; a failed step's log is in $LOG/<n>.log (default /tmp/paperboat-verify).
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD; SHARED=$ROOT/../paperboat-shared; STUDIO=$ROOT/../squero-studio-om
PB=$ROOT/engine/build/tools/paperboat
QUICK=0; [ "$1" = "--quick" ] && QUICK=1
LOG=${LOG:-/tmp/paperboat-verify}; mkdir -p "$LOG"; rm -f "$LOG"/*.log
fail=0; n=0; rows=()

step() { # name dir cmd...
  local name=$1 dir=$2; shift 2; n=$((n+1))
  local t=$SECONDS
  if (cd "$dir" && "$@") >"$LOG/$n.log" 2>&1; then rows+=("ok    $name ($((SECONDS-t))s)")
  else rows+=("FAIL  $name  -> $LOG/$n.log"); fail=1; fi
}

[ -d "$SHARED" ] || { echo "missing $SHARED (clone paperboat-shared next to this repo)"; exit 1; }

step "shared: typecheck"          "$SHARED" npm run -s typecheck
step "shared: tests"              "$SHARED" npm test
if [ -d "$STUDIO" ]; then
  step "studio: typecheck"        "$STUDIO" npm run -s typecheck
  step "studio: tests"            "$STUDIO" npm test
else
  rows+=("skip  studio (not checked out)")
fi
step "paperboat: protocol.ts current" "$ROOT" python3 protocol/gen_ts.py --check
step "paperboat: tokens current"  "$ROOT" python3 docs/gen_tokens.py --check
step "paperboat: no bundled toolkits" "$ROOT" python3 scripts/check_bundle.py --self-test
step "paperboat: typecheck"       "$ROOT" npm run -s typecheck
step "paperboat: web+cli tests"   "$ROOT" npm test -w @paperboat/web -w @paperboat/cli
if [ $QUICK = 0 ]; then
  step "studio: build"            "$STUDIO" npm run -s build
  step "paperboat: web build"     "$ROOT"   npm run -s build
  step "engine: build"            "$ROOT"   cmake --build engine/build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
  step "engine: unit tests"       "$ROOT"   engine/build/tests/paperboat_tests
  step "engine: check sdk-backend" "$ROOT" "$PB" check engine/tests/sdk-backend
  step "paperboat: install tree clean" "$ROOT" sh -c 'rm -rf engine/build/bundle-check && cmake --install engine/build --prefix engine/build/bundle-check >/dev/null && python3 scripts/check_bundle.py --prefix engine/build/bundle-check'
  step "docs: build (-W)"         "$ROOT"   ./scripts/build_docs.sh
fi

echo; printf '%s\n' "${rows[@]}"
[ $fail = 0 ] && echo "ALL OK" || echo "FAILED"
exit $fail
