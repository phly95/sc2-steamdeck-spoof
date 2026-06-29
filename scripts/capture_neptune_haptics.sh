#!/bin/bash
# Capture haptic output reports Steam sends to Neptune controller (/dev/hidraw3)
#
# Usage:
#   1. Stop sc2-hogp: sudo systemctl stop sc2-hogp
#   2. Run this script: sudo bash /tmp/capture_neptune_haptics.sh
#   3. Start Steam on the Deck
#   4. Click the trackpad
#   5. Observe captured haptic reports
#   6. Ctrl+C to stop

set -e

echo "[*] Finding Steam process with /dev/hidraw3 open..."

# Find PIDs that have /dev/hidraw3 open
PIDS=$(sudo lsof /dev/hidraw3 2>/dev/null | awk 'NR>1{print $2}' | sort -u)

if [ -z "$PIDS" ]; then
    echo "[!] No process has /dev/hidraw3 open."
    echo "[!] Make sure Steam is running and Neptune controller is connected."
    echo ""
    echo "[*] Trying to find Steam process anyway..."
    PIDS=$(pgrep -f "steam" 2>/dev/null | head -5)
    if [ -z "$PIDS" ]; then
        echo "[!] No Steam process found. Start Steam first."
        exit 1
    fi
fi

echo "[*] Found PIDs: $PIDS"

# For each PID, find the fd for /dev/hidraw3
for PID in $PIDS; do
    echo ""
    echo "=== PID $PID ==="
    echo "[*] Open files:"
    sudo ls -la /proc/$PID/fd/ 2>/dev/null | grep hidraw || echo "  (no hidraw fds)"

    # Find the fd number for /dev/hidraw3
    FD=$(sudo ls -la /proc/$PID/fd/ 2>/dev/null | grep hidraw3 | awk -F'->' '{print $1}' | awk '{print $NF}')
    if [ -n "$FD" ]; then
        echo "[*] /dev/hidraw3 is fd=$FD"
    fi
done

echo ""
echo "[*] Starting strace on PIDs to capture writes..."
echo "[*] Click the trackpad now!"
echo "[*] Press Ctrl+C to stop"
echo ""

# Strace all found PIDs, capture write() calls with full data
# -e trace=write: only trace write syscalls
# -s 256: show up to 256 bytes of string data
# -p: attach to process
# -f: follow forks
sudo strace -e trace=write -f -s 256 -p $(echo $PIDS | tr ' ' '|' | sed 's/|/ -p /g') 2>&1 | \
    grep -E "write\(|= [0-9]" | \
    while IFS= read -r line; do
        # Show all writes with their data
        echo "[$(date +%H:%M:%S.%N)] $line"
    done
