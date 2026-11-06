#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Locate paperboat-examples directory
if [ -n "$PAPERBOAT_EXAMPLES_DIR" ] && [ -d "$PAPERBOAT_EXAMPLES_DIR" ]; then
  EXAMPLES_DIR="$PAPERBOAT_EXAMPLES_DIR"
elif [ -d "$REPO_ROOT/../paperboat-examples" ]; then
  EXAMPLES_DIR="$REPO_ROOT/../paperboat-examples"
elif [ -d "/home/cyyth/.local/share/opencode/worktree/f0d43b/paperboat-examples" ]; then
  EXAMPLES_DIR="/home/cyyth/.local/share/opencode/worktree/f0d43b/paperboat-examples"
else
  echo "Error: paperboat-examples directory not found. Please set PAPERBOAT_EXAMPLES_DIR." >&2
  exit 1
fi

echo "Using examples directory: $EXAMPLES_DIR"

# Ensure old instances are closed first
"$SCRIPT_DIR/close_all_examples.sh"

export PYTHONPATH="$REPO_ROOT/python:$PYTHONPATH"

# Always ensure Python bindings are up-to-date
echo "==> Updating Python bindings..."
(cd "$REPO_ROOT" && npm run build:python) || echo "Warning: build:python had errors"

echo "==> Launching backends..."

launch_proc() {
  local port="$1"
  local workdir="$2"
  shift 2
  local cmd=("$@")

  if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --unit="paperboat-backend-${port}" \
      --setenv=PYTHONPATH="$PYTHONPATH" \
      --working-directory="$workdir" \
      "${cmd[@]}" >/dev/null 2>&1; then
    :
  else
    (cd "$workdir" && nohup "${cmd[@]}" > "/tmp/backend-${port}.log" 2>&1 &)
  fi
  echo "Started backend on port ${port}"
}

# 1. 8765: mass-spring-python
launch_proc 8765 "$EXAMPLES_DIR/mass-spring-python" python3 server.py

# 2. 8766: cart-pole-cpp
echo "==> Building cart-pole-cpp..."
if cmake -S "$EXAMPLES_DIR/cart-pole-cpp" -B "$EXAMPLES_DIR/cart-pole-cpp/build" >/dev/null && \
   cmake --build "$EXAMPLES_DIR/cart-pole-cpp/build"; then
  launch_proc 8766 "$REPO_ROOT" "$EXAMPLES_DIR/cart-pole-cpp/build/paperboat-cart-pole" --port 8766
else
  echo "cart-pole-cpp build failed, skipping"
fi

# 3. 8767: parametric-python
launch_proc 8767 "$EXAMPLES_DIR/parametric-python" python3 server.py

# 4. 8768: runner-python
launch_proc 8768 "$EXAMPLES_DIR/runner-python" python3 server.py

# 5. 8769: animation-python
launch_proc 8769 "$EXAMPLES_DIR/animation-python" python3 server.py

# 6. 8770: parts-cpp
echo "==> Building parts-cpp..."
if cmake -S "$EXAMPLES_DIR/parts-cpp" -B "$EXAMPLES_DIR/parts-cpp/build" >/dev/null && \
   cmake --build "$EXAMPLES_DIR/parts-cpp/build"; then
  launch_proc 8770 "$REPO_ROOT" "$EXAMPLES_DIR/parts-cpp/build/paperboat-parts" --port 8770
else
  echo "parts-cpp build failed, skipping"
fi

# 7. 8771: chain-python
launch_proc 8771 "$EXAMPLES_DIR/chain-python" python3 server.py

# 8. 8772: supply-python
launch_proc 8772 "$EXAMPLES_DIR/supply-python" python3 server.py

# 9. 8773: double-pendulum-cpp
echo "==> Building double-pendulum-cpp..."
if cmake -S "$EXAMPLES_DIR/double-pendulum-cpp" -B "$EXAMPLES_DIR/double-pendulum-cpp/build" >/dev/null && \
   cmake --build "$EXAMPLES_DIR/double-pendulum-cpp/build"; then
  launch_proc 8773 "$REPO_ROOT" "$EXAMPLES_DIR/double-pendulum-cpp/build/paperboat-double-pendulum" --port 8773
else
  echo "double-pendulum-cpp build failed, skipping"
fi

echo "==> All example backends launched (ports 8765-8773)."
