#!/usr/bin/env bash

PORTS=(8765 8766 8767 8768 8769 8770 8771 8772)

echo "==> Stopping all example backends..."

# Stop systemd user units if running
if command -v systemctl >/dev/null 2>&1; then
  for p in "${PORTS[@]}"; do
    systemctl --user stop "paperboat-backend-${p}.service" 2>/dev/null || true
  done
fi

# Kill any processes on the designated ports
if command -v fuser >/dev/null 2>&1; then
  for p in "${PORTS[@]}"; do
    fuser -k "${p}/tcp" 2>/dev/null || true
  done
fi

# Also clean up known binary / server process names if orphaned
pkill -f "paperboat-cart-pole" 2>/dev/null || true
pkill -f "paperboat-parts" 2>/dev/null || true
pkill -f "paperboat-double-pendulum" 2>/dev/null || true
pkill -f "python3.*server\.py" 2>/dev/null || true

# Wait until ports are freed
for p in "${PORTS[@]}"; do
  for _ in {1..20}; do
    if ! fuser "${p}/tcp" >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done
done

echo "==> All example backends stopped."
