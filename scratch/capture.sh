#!/bin/bash
# Host capture script
# Must be run with sudo privilege for btmon

LOG_FILE="/home/philip/steamdeck-sc2-spoof/scratch/btmon_handshake.log"
rm -f "$LOG_FILE"

echo "[capture] Starting btmon..."
btmon -w "$LOG_FILE" &
BTMON_PID=$!
sleep 1

echo "[capture] Removing device C2:12:34:56:78:9A..."
bluetoothctl remove C2:12:34:56:78:9A
sleep 2

echo "[capture] Connecting to C2:12:34:56:78:9A..."
bluetoothctl connect C2:12:34:56:78:9A

echo "[capture] Waiting 10 seconds for handshake and errors..."
sleep 10

echo "[capture] Stopping btmon..."
kill $BTMON_PID
wait $BTMON_PID 2>/dev/null

echo "[capture] Done."
