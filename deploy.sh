#!/usr/bin/env bash
# deploy.sh — Build VoidShell and send to PS5
#
# Usage:
#   ./deploy.sh <ps5-ip>           # build + copy www + send ELF
#   ./deploy.sh <ps5-ip> --no-build  # skip rebuild, just send
#
# Requires: ps5-payload-sdk in $PS5_PAYLOAD_SDK (or /opt/ps5-payload-sdk)
# The PS5 must be jailbroken and listening on port 9021.

set -e

PS5_IP="${1:-}"
NO_BUILD=0
PORT=9021
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---- Argument parsing ----
for arg in "$@"; do
    case "$arg" in
        --no-build) NO_BUILD=1 ;;
    esac
done

if [ -z "$PS5_IP" ]; then
    echo "Usage: $0 <ps5-ip> [--no-build]"
    echo "  Example: $0 192.168.1.17"
    exit 1
fi

echo "=================================================="
echo " VoidShell Deploy"
echo " Target: $PS5_IP:$PORT"
echo "=================================================="

# ---- Step 1: Build ----
if [ "$NO_BUILD" -eq 0 ]; then
    echo ""
    echo "[1/3] Building..."
    cd "$SCRIPT_DIR"
    make clean && make
    echo "      Build OK: build/voidshell.elf"
else
    echo "[1/3] Skipping build (--no-build)"
fi

# ---- Step 2: Copy www assets ----
echo ""
echo "[2/3] Copying www assets..."
cd "$SCRIPT_DIR"
make www
echo "      www OK: build/www/index.html"

# ---- Step 3: Send ELF to PS5 ----
echo ""
echo "[3/3] Sending build/voidshell.elf to $PS5_IP:$PORT ..."

ELF="$SCRIPT_DIR/build/voidshell.elf"

if [ ! -f "$ELF" ]; then
    echo "ERROR: $ELF not found. Run without --no-build first."
    exit 1
fi

FILE_SIZE=$(wc -c < "$ELF")
echo "      File size: $FILE_SIZE bytes"

# Use nc with -w 5 (timeout) — same as original workflow
if command -v nc &>/dev/null; then
    nc -w 5 "$PS5_IP" "$PORT" < "$ELF"
    echo "      Sent via nc"
elif command -v socat &>/dev/null; then
    socat - TCP:"$PS5_IP":"$PORT",crnl < "$ELF"
    echo "      Sent via socat"
else
    echo "ERROR: Neither nc nor socat found. Install one and retry."
    exit 1
fi

echo ""
echo "=================================================="
echo " Done! VoidShell sent to PS5."
echo " Dashboard: http://$PS5_IP:7007"
echo "=================================================="
