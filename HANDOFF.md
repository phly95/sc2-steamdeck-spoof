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
- **Haptic forwarding code ready** — `_on_haptic_write()` handler on handle 0x0019 correctly parses both 10-byte (with Report ID) and 9-byte (stripped) haptic payloads and forwards to Neptune. However, **the host never sends haptic output reports** — btmon capture confirmed zero ATT Write Command (0x52) packets. The issue is upstream in Steam/hog-ll.
- **Feature Report Proxy to Neptune**: Non-SC2 Feature Reports proxied to Neptune hardware via `ioctl`.
- **Steam Client SC2 Recognition**: Steam detects Type 10 (Neptune/SC2), ProductID 4867 (0x1303), loads `controller_neptune.vdf`, auto-registers controller. 45-byte SC2 Custom reports (Report ID 0x45) verified flowing to `/dev/hidrawN` via hexdump.
- **Bonding Key Mismatch Fix**: After Deck BT restart, stale LTK on host causes `[Errno 38] ENOSYS` on `conn.recv()`. Fix: `bluetoothctl remove C2:12:34:56:78:9A` then re-pair.
- **Comprehensive Diagnostic Logging** (`[DIAG]` tagged).

---

## 3. What Needs to be Done

### 1. Fix SET_SETTINGS Register 0x09 Verification Loop (PRIMARY BLOCKER)
- **Status**: Steam sends SET_SETTINGS 0x09 (lizard mode OFF) every 3 seconds. **Steam NEVER reads FR 0x00 back to verify** — zero ATT Read Requests observed in btmon and Deck logs. The verification path in `vtable[0x130]` (get_feature_report) is never triggered. Root cause likely the `set_report_cb()` error putting BlueZ's HOG profile in a broken state where it cannot send feature report reads.
- **RE findings**: The SET_SETTINGS buffer format is `[0x01, 0x87, 0x03, register, value_lo, value_hi, ...]`. Register 0x09 = SETTING_LIZARD_MODE, value 0 = LIZARD_MODE_OFF. Lizard mode must be OFF for haptics to work. The verification reads FR 0x00 back and does a byte-by-byte comparison of the echoed command bytes.
- **Known bug**: `payload_len + 1` (line 464, main_l2cap.py) should be `payload_len` — the SET_SETTINGS response length byte is 0x04 instead of 0x03.
- **Critical log finding**: The write goes to handle 0x0024 (FR 0x00 value) with 65 bytes of data starting with `01 87 03 09 00 00...` (Report ID 0x01 at byte 0). The callback fires with report_id=0x00. No ATT Read Requests follow — the verification never reads FR 0x00.
- **What to try next**:
  1. Fix the `set_report_cb()` error — BlueZ returns ATT Error 0x0E when our ATT server can't handle a SET_REPORT write. This may be putting the HOG profile in a state where it can't send feature report reads.
  2. Fix the `payload_len + 1` → `payload_len` bug
  3. Add diagnostic logging to `_handle_write` to see which handle BlueZ tries to SET_REPORT on
  4. Check if the HOG profile's pending SET_REPORT blocks all subsequent reads

### 2. Investigate `set_report_cb()` ATT Error on Host (LIKELY ROOT CAUSE)
- **Status**: `hog-lib.c:set_report_cb() Error setting Report value: Request attribute has encountered an unlikely error` appears at connection time. btmon capture confirmed hog-ll discovers the output report handle (0x0019) but **never writes to it**. This error may put hog-ll in a state where it can't send output reports — explaining why zero ATT Write Command (0x52) packets are sent.
- **What to try next**:
  1. Determine which handle hog-ll is writing to when this error occurs (add diagnostic logging to `_handle_write`)
  2. Check if the handle has correct permissions (added ATT_PROP_WRITE to output report characteristics)
  3. Check if the error is related to the BLE path specifically (BLE vs USB code paths)

### 3. Haptic Feedback (HOST NOT SENDING)
- **Status**: The haptic forwarding code is ready and correct — `_on_haptic_write()` on handle 0x0019 parses both 10-byte and 9-byte payloads. However, **the host never sends haptic output reports** — btmon capture confirmed zero ATT Write Command (0x52) packets during a test session. The issue is upstream in Steam/hog-ll.
- **RE findings**: Haptics use `SDL_hid_write()` (output reports, NOT feature reports). Lizard mode must be OFF for haptics to work. The SET_SETTINGS 0x09 loop may be blocking haptics because Steam thinks lizard mode is still enabled.
- **What to try next**:
  1. Fix the `set_report_cb()` error — this may be blocking hog-ll from sending output reports
  2. Fix the SET_SETTINGS 0x09 loop — lizard mode must be OFF for haptics
  3. Get a real SC2 btmon capture to see the correct verification response

### 4. Dual Trackpads & IMU (Gyro/Accel) Forwarding
- **Status**: 45-byte SC2 Custom report with trackpad X/Y, IMU (accel/gyro), and force sensors is **already implemented** in `input_handler.py`. The data flows correctly from Neptune HID → SC2 report.
- **Remaining**: Steam may need specific settings enabled to activate gyro/trackpad features (registers 0x27 IMU_MODE, etc.).

### 3. Auto-Reconnect Daemon
- **Status**: Advertising refresh on disconnect is **already implemented** in `main_l2cap.py:_schedule_adv_refresh()`.
- **Remaining**: Ensure clean re-advertising after disconnects without manual intervention.

### 4. SET_SETTINGS Loop (Known Blocker)
- **Status**: Steam sends SET_SETTINGS 0x09 every 3 seconds. **Zero ATT Read Requests** — the verification never reads FR 0x00 back. The `BYieldingCompleteSteamControllerRegistration` flow blocks at `EYldWaitForControllerDetails`.
- **Bug found**: `payload_len + 1` in SET_SETTINGS response should be `payload_len` (length byte 0x04 → 0x03).
- **Root cause hypothesis**: The `set_report_cb()` ATT error (BlueZ returns 0x0E to a SET_REPORT Write Request) puts the HOG profile in a broken state where it cannot send feature report reads. This explains why the verification never reads FR 0x00.
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

### 5. Reverse Engineering Findings (from steamclient.so)
- **ControllerDetails_tE**: 84 bytes (0x54), ready_flag at offset 0x3c must be 1. Set by QueueFetchingControllerDetails at 0x01092820. Fields come from controller object offsets 0x84-0xd4.
- **Product ID check**: 0x1303 is in recognized range (0x1302-0x1305). Other recognized types: 0x1142, 0x1220, 0x1201-0x1206, 0x1101-0x1102.
- **Haptic path**: Uses SDL_hid_write() (output reports), NOT SDL_hid_send_feature_report(). Report ID 0x80, 10 bytes. Lizard mode must be OFF for haptics to work.
- **SET_SETTINGS format**: `[0x01, 0x87, 0x03, register, value_lo, value_hi, ...]`. Register 0x09 = SETTING_LIZARD_MODE, value 0 = OFF.
- **0xf2 command**: Per-category capability query dispatched via switch/case. Exact response format unknown.
- **RE session files**: ~/steamclient-reverse-session/ contains findings.md, functions/, notes/

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
