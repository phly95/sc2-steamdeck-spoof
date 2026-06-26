# Handoff Guide — Steam Deck to SC2 BLE Spoof Project

This document provides a summary of the current architecture, what is currently working, what needs to be done next, and the recommended approaches.

---

## 1. Project Goal & Current Architecture
We present the **Steam Deck** as a **Steam Controller 2026 (SC2 / Triton)** over **Bluetooth Low Energy** so that the host PC's Steam Client recognizes it natively and supports full Steam Input features (trackpads, gyro, haptics, back buttons).

### Architecture Overview
```
┌──────────────────────────────────────────────────────────┐
│                    Steam Deck (Peripheral)                 │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │  main_l2cap.py                                      │ │
│  │  ├─ GLib main loop (BlueZ D-Bus advertising)        │ │
│  │  ├─ Agent1 (auto-confirm pairing via dbus-python)   │ │
│  │  └─ Raw L2CAP ATT server thread                     │ │
│  │     └─ Binds to C2:12:34:56:78:9A CID 4             │ │
│  │     └─ Handles ATT PDU exchange (MTU, discovery)    │ │
│  │     └─ Serves GATT database (82 attributes, 6 services)│ │
│  └─────────────────────────────────────────────────────┘ │
│                                                          │
│  BlueZ handles:                                          │
│  ├─ SMP pairing (kernel, CID 6)                          │
│  └─ LE advertising (LEAdvertisingManager1)               │
│                                                          │
│  Input Source: /dev/hidraw3 (Neptune HID state reports)   │
│  ├─ input_handler.py reads 64-byte Neptune reports       │
│  ├─ Maps buttons/sticks/triggers to SC2 format           │
│  └─ Sends as ATT notifications (Mouse/Keyboard/Gamepad)  │
└──────────────────────────────────────────────────────────┘
              │
              │ BLE (static random addr C2:12:34:56:78:9A)
              ▼
┌──────────────────────────────────────────────────────────┐
│                    Host PC (Central)                      │
│                                                          │
│  BlueZ hog-ll driver → /dev/hidrawN                      │
│   └─ Standard evdev events (eventN) for Mouse/Keyboard   │
│   └─ Steam Client reads raw hidraw for Steam Input       │
└──────────────────────────────────────────────────────────┘
```

---

## 2. What is Working
- **Raw L2CAP ATT Server (CID 4)**: Bypasses the SteamOS BlueZ GATT listener socket binding bug.
- **Just Works Pairing**: Auto-confirm via D-Bus `Agent1`.
- **GATT Database (74 attributes, 6 services)**: GAP, GATT, HID (0x1812) with CHR_REPORT for SC2 Custom + Haptic Output, Valve Custom HID Service, Battery, Device Information.
- **PnP ID**: USB-IF source (0x02), Valve VID (0x28DE), PID (0x1303).
- **Physical Deck Input Capture**: Reads Neptune controller `/dev/hidraw3` (64-byte HID reports).
- **Neptune Auto-Recovery**: Reopens hidraw on crash (2s delay, 10 retries).
- **45-byte SC2 Custom Reports**: Full Triton 32-bit button bitmask (verified from SDL3 `TritonButtons` enum), analog sticks, triggers, trackpads, IMU, force sensors. Sent on CHR_REPORT handles 0x0033 and 0x003c.
- **Standard HID Gamepad Reports**: 12-byte reports on handle `0x0012` with buttons, analog sticks (Y axis corrected), triggers. Host creates `/dev/input/eventN`.
- **Lizard Mode Mouse/Keyboard**: Relative mouse (right trackpad) and keyboard reports on handles `0x0019`/`0x001d`.
- **Synthetic SC2 Command Handler**: Feature Report 0x00 intercepts SC2 commands:
  - `0x83` GET_ATTRIBUTES - responds with synthetic device info (capabilities bitmask 0x4169bfff)
  - `0xAE` GET_SERIAL - responds with serial number
  - `0xBA` GET_CHIP_ID - responds with 15-byte chip ID
  - `0x87` SET_SETTINGS - acknowledges, stores register values
  - `0x89` GET_SETTINGS_VALUES - returns stored register values
  - `0x81` CLEAR_MAPPINGS - acknowledges
  - `0x85` SET_DEFAULT_DIGITAL_MAPPINGS - handles mode switch
  - Unknown commands echoed with zero payload
- **Haptic Rumble Forwarding (verified end-to-end)**: Host writes haptic output report to `/dev/hidrawN`, hog-ll strips Report ID 0x80 and sends 9-byte ATT Write Command (0x52) to handle 0x0019, Deck parses and forwards rumble to Neptune controller via `os.write()` on `/dev/hidraw3`. Verified via btmon capture and Deck logs.
- **Feature Report Proxy to Neptune**: Non-SC2 Feature Reports proxied to Neptune hardware via `ioctl`.
- **Steam Client SC2 Recognition**: Steam detects Type 10 (Neptune/SC2), ProductID 4867 (0x1303), loads `controller_neptune.vdf`, auto-registers controller. 45-byte SC2 Custom reports (Report ID 0x45) verified flowing to `/dev/hidrawN` via hexdump.
- **Bonding Key Mismatch Fix**: After Deck BT restart, stale LTK on host causes `[Errno 38] ENOSYS` on `conn.recv()`. Fix: `bluetoothctl remove C2:12:34:56:78:9A` then re-pair.
- **Comprehensive Diagnostic Logging** (`[DIAG]` tagged).

---

## 3. What Needs to be Done

### 1. Fix SET_SETTINGS Register 0x09 Retry Loop (PRIMARY BLOCKER)
- **Status**: After the initial handshake completes, Steam retries SET_SETTINGS register 0x09 (value=0x0000) every 3 seconds. `BYieldingCompleteSteamControllerRegistration` blocks at `EYldWaitForControllerDetails`. Steam does NOT read FR 0x00 back after this write — the retry is write-only. Sending CHR_REPORT notifications didn't break the loop.
- **IMPORTANT UPDATE**: Another agent cleared stale bonding keys on the host (`bluetoothctl remove C2:12:34:56:78:9A`) and re-paired. After this, the handshake completed successfully and 45-byte SC2 Custom reports were verified flowing to the host. **The bonding key mismatch (gotcha #9) was likely the root cause** — stale LTK caused connection instability that prevented the handshake from completing properly. The protocol response fixes (capabilities bitmask, early return, etc.) may have been necessary but not sufficient on their own.
- **Bugs fixed this session**:
  1. **`SC2_CMD_SET_MODE` AttributeError** — `main_l2cap.py:456` referenced `self.SC2_CMD_SET_MODE` but the constant was `SC2_CMD_SET_CONTROLLER_MODE`. This crashed every 0xf2 command silently, causing Steam to see empty capability data. Fixed to use `SC2_CMD_SET_DEFAULT_MAPPINGS` (the correct constant for 0x85).
  2. **Capabilities bitmask zeroed** — `ATTRIB_CAPABILITIES` (tag 2 in GET_ATTRIBUTES) was `0x00000000`. Changed to `0x4169bfff` (real SC2 value from controller logs).
  3. **SET_SETTINGS early return** — `return` before `self._pending_fr_response[report_id]` was set meant Steam never got SET_SETTINGS acknowledgment via FR 0x00 read. Fixed by removing the early return and improving the response format to include register + value echo.
- **Root cause hypothesis**: The 0xf2 command response is still `[0xf2, 0x00, zeros]`. This command has a 1-byte payload (byte 2 varies per invocation: 0x01, 0x02, etc.) and is likely a per-category capabilities/settings query. Returning zeros tells Steam the controller has empty settings, preventing `ControllerDetails_tE` from being fully populated. **Without a btmon capture of a real SC2 handshake, we cannot determine the correct 0xf2 response format.**
- **What to try next**:
  1. Verify the SET_SETTINGS loop is gone after the bonding fix by checking Deck logs for 3+ minutes
  2. If the loop persists, capture a real SC2 handshake via `btmon -t -w /tmp/sc2_handshake.log` on the host while pairing a real SC2 controller
  3. Analyze the Feature Report read/write sequence, specifically the 0xf2 responses
  4. Try different 0xf2 response payloads (e.g., capability bitmask, register values)

### 2. Investigate `set_report_cb()` ATT Error on Host
- **Status**: `hog-lib.c:set_report_cb() Error setting Report value: Request attribute has encountered an unlikely error` appears at connection time. hog-ll fails to set an output report via ATT. This may be related to the bonding key mismatch (gotcha #9) — when the host uses stale keys, the connection aborts mid-handshake, causing ATT errors.
- **What to try next**:
  1. Clear bonding keys and re-pair — the error may disappear with a clean connection
  2. If it persists, add diagnostic logging to `_handle_write` to identify the failing handle
  3. Check if the handle has correct permissions (WRITE vs WRITE_NO_RSP)

### 3. Haptic Feedback (VERIFIED WORKING)
- **Status**: Haptics work end-to-end. Host writes 10-byte haptic output report to `/dev/hidrawN`, hog-ll strips Report ID 0x80 and sends 9-byte ATT Write Command (0x52) to handle 0x0019, Deck parses and forwards rumble to Neptune controller via `os.write()` on `/dev/hidraw3`. Verified via btmon capture and Deck logs.
- **Key finding**: hog-ll strips the Report ID byte before the ATT write. The `_on_haptic_write()` handler now handles both formats: 10-byte (with Report ID 0x80) and 9-byte (stripped).

### 4. Dual Trackpads & IMU (Gyro/Accel) Forwarding
- **Status**: 45-byte SC2 Custom report with trackpad X/Y, IMU (accel/gyro), and force sensors is **already implemented** in `input_handler.py`. The data flows correctly from Neptune HID → SC2 report.
- **Remaining**: Steam may need specific settings enabled to activate gyro/trackpad features (registers 0x27 IMU_MODE, etc.).

### 3. Auto-Reconnect Daemon
- **Status**: Advertising refresh on disconnect is **already implemented** in `main_l2cap.py:_schedule_adv_refresh()`.
- **Remaining**: Ensure clean re-advertising after disconnects without manual intervention.

### 4. SET_SETTINGS Loop (Known Blocker)
- **Status**: After the initial handshake, Steam falls into a SET_SETTINGS register 0x09 retry loop every 3 seconds. Never reaches SET_MODE.
- **Bugs fixed this session**:
  1. `SC2_CMD_SET_MODE` AttributeError — 0xf2 commands were crashing silently (was causing 8x retries, now 1x)
  2. Capabilities bitmask zeroed → fixed to 0x4169bfff
  3. SET_SETTINGS early return → FR 0x00 read never got the response
- **Root cause**: The `BYieldingCompleteSteamControllerRegistration` flow blocks at `EYldWaitForControllerDetails`. The registration flow involves:
  1. `CHIDIOThread::DiscoverNewControllers()` → enumerate
  2. `CHIDIOThread::ConnectController()` → connect
  3. `QueueFetchingControllerDetails()` → get ControllerDetails_tE
  4. `YldInitialControllerStateEnumerated: waiting on details` ← **BLOCKED HERE**
  5. `WaitInitialControllerStateEnumerated_Request/Response`
  6. `CClientJobCompleteControllerRegistration` → proceeds
- **Key commands in the SC2 protocol flow**:
  1. `0x83` GET_ATTRIBUTES → response: `[0x83, 0x2D, 9 attributes x 5 bytes, padding]`
  2. `0xF2` Unknown (1-byte payload varies: 0x01, 0x02, etc.) → response: `[0xF2, 0x00, zeros]` (STILL WRONG — needs real SC2 capture)
  3. `0xAE` GET_SERIAL → response: `[0xAE, 0x14, 0x01, serial_ascii, padding]`
  4. `0xBA` GET_CHIP_ID → response: `[0xBA, 0x11, 0x00, 15-byte chip_id, padding]`
  5. `0x87` SET_SETTINGS → write-only (configures registers), now includes ack notification
  6. `0x89` GET_SETTINGS_VALUES → response: stored register values
  7. `0xC1`/`0xDC`/0xE2` Unknown → echo with zero payload
  8. `0x81` CLEAR_MAPPINGS → write-only (exits lizard mode)
  9. `0x85` SET_DEFAULT_DIGITAL_MAPPINGS → write-only (enters gamepad mode)
  10. `0x8D` SET_CONTROLLER_MODE → mode switch (lizard ↔ Steam Input)

---

## 4. How to Run & Verify

### Start the Service on the Deck
```bash
# 1. Restart bluetooth and apply custom LE config
echo <DECK_PASSWORD> | sudo -S systemctl stop sc2-hogp bluetooth
echo <DECK_PASSWORD> | sudo -S systemctl start bluetooth
sleep 2
echo <DECK_PASSWORD> | sudo -S python3 /tmp/config_bt.py

# 2. Run the deployment script to copy latest code and start the service
./scripts/deploy.sh
```

### Connect on the Host
```bash
# Connect using bluetoothctl (avoid 'pair' to prevent BR/EDR classic bonding timeouts)
bluetoothctl connect C2:12:34:56:78:9A
```

### Listen to Input Events on the Host
```bash
# Find and monitor relative mouse movement and keypress events
echo <HOST_SUDO_PASSWORD> | sudo -S python3 -u scratch/listen_events.py
```
