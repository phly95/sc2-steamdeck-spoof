# SC2 Clone Guide

How to build a Steam Controller 2026 (SC2) clone that Steam Client recognizes with full Steam Input support.

## Overview

The SC2 communicates with the host PC over **Bluetooth Low Energy** using a custom HID profile. A clone needs to:

1. Present itself as an SC2 via BLE advertising (PID `0x1303`)
2. Serve a GATT database matching the SC2 layout
3. Send 45-byte input reports (Report ID `0x45`)
4. Handle feature reports (command channel)
5. Accept haptic output reports

## Step 1: GATT Services

**Read**: `docs/sc2-protocol.md` (GATT Services section) and `docs/att-server-implementation.md` (Handle Layout table)

The SC2 exposes these services:

| Service | UUID | Notes |
|---------|------|-------|
| GAP | `0x1800` | Device name + appearance (gamepad) |
| GATT | `0x1801` | Service changed indicator |
| HID | `0x1812` | HID Information, Report Map, Protocol Mode, Reports |
| Valve Custom | `100f6c32-...` | Steam reads from these characteristics directly |
| Battery | `0x180F` | BlueZ HOGP requirement (not in real firmware, but needed for host) |
| Device Info | `0x180A` | PnP ID with VID=0x28DE, PID=0x1303 |

**Code reference**: `src/gatt_db.py` — `build_sc2_database()` builds the complete GATT database.

## Step 2: HID Report Descriptor

**Read**: `src/gatt_db.py` — `build_report_map()` method

The report map defines:
- **Report ID 1**: Gamepad input (12 bytes) — sticks, triggers, buttons
- **Report ID 2**: Output (1 byte)
- **Report ID 3**: Mouse input (4 bytes)
- **Report ID 4**: Keyboard input (8 bytes)
- **Report ID 0x45**: SC2 custom input (45 bytes) — **primary input**
- **Report ID 0x47**: SC2 custom input (47 bytes)
- **Report ID 0x80**: Haptic rumble output (9 bytes)

## Step 3: Input Report Format

**Read**: `research/triton_hid_analysis.md` (0x45 Layout section)

The 45-byte Report ID `0x45`:

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 1 | Sequence counter |
| 0x01-0x04 | 4 | Flags + 20-bit button bitmask |
| 0x05-0x06 | 2 | Left trigger (uint16) |
| 0x07-0x08 | 2 | Right trigger (uint16) |
| 0x09-0x0C | 4 | Left stick X/Y (int16) |
| 0x0D-0x10 | 4 | Right stick X/Y (int16) |
| 0x11-0x16 | 6 | Gyro X/Y/Z (uint16) |
| 0x17-0x1C | 6 | Accel X/Y/Z (uint16) |
| 0x1D-0x2C | 16 | Trackpad left/right (8 bytes each) |

**Button bitmask** (from `research/ibex_command_table.md`):

| Bit | Button |
|-----|--------|
| 0 | A |
| 1 | B |
| 2 | X |
| 3 | Y |
| 4 | D-pad Up |
| 5 | D-pad Down |
| 6 | D-pad Left |
| 7 | D-pad Right |
| 8 | Left bumper |
| 9 | Right bumper |
| 10 | Left stick click |
| 11 | Right stick click |
| 12 | Left paddle (L4) |
| 13 | Right paddle (R4) |
| 14 | Left paddle (L5) |
| 15 | Right paddle (R5) |
| 16 | Steam/Home |
| 20 | Left trackpad touch |
| 21 | Right trackpad touch |
| 22 | Left trackpad click |
| 23 | Right trackpad click |

## Step 4: Command Handling

**Read**: `research/ibex_command_table.md` (full 100-command table)

The host sends commands via Feature Report ID `0x00`. The clone must handle at minimum:

| Command | Code | Response |
|---------|------|----------|
| GET_ATTRIBUTES | `0x83` | Attribute tag/value pairs (VID, PID, capabilities) |
| GET_SERIAL | `0xAE` | 20-byte serial string |
| CLEAR_MAPPINGS | `0x81` | Acknowledgement |
| SET_HAPTIC | `0x80` | Haptic output to motors |

**Code reference**: `src/main_l2cap.py` — `_on_sc2_command()` handles the command channel.

## Step 5: Haptic Output

**Read**: `docs/sc2-protocol.md` (Haptic Output Report section)

Report ID `0x80` (9 bytes):

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 1 | Left motor speed |
| 0x01-0x03 | 3 | Left motor amplitude/timing |
| 0x04 | 1 | Right motor speed |
| 0x05-0x07 | 3 | Right motor amplitude/timing |
| 0x08 | 1 | Padding |

## Step 6: ATT Server

**Read**: `docs/att-server-implementation.md` (ATT Opcode Table section)

The clone needs a raw L2CAP ATT server on CID 4. Key ATT operations:

| Opcode | Name | Purpose |
|--------|------|---------|
| `0x02`/`0x03` | MTU Exchange | Negotiate MTU (up to 517) |
| `0x10`/`0x11` | Read By Group Type | Service discovery |
| `0x08`/`0x09` | Read By Type | Characteristic discovery |
| `0x04`/`0x05` | Find Information | Descriptor discovery |
| `0x0A`/`0x0B` | Read Request | Read characteristic values |
| `0x12`/`0x13` | Write Request | Write CCCD, handle commands |
| `0x1B` | Handle Notification | Send input reports |

**Code reference**: `src/att_server.py` — complete ATT server implementation.

## Working Reference Implementation

The `spoofdeck` repo contains a complete working implementation:

| File | Purpose |
|------|---------|
| `src/gatt_db.py` | GATT database (all services, characteristics, descriptors) |
| `src/att_server.py` | Raw L2CAP ATT server (handles all ATT PDUs) |
| `src/main_l2cap.py` | Entry point (BLE advertising, pairing, ATT server) |
| `src/input_handler.py` | Neptune → SC2 input mapping (reference for report format) |
| `src/agent.py` | BlueZ Agent1 (auto-confirm pairing) |
| `src/adv.py` | BLE advertisement |

## Known Issues

- **Steam haptics**: UI/trackpad haptics don't work (0x8F gate in steamclient.so blocks them). In-game rumble via `SDL_RumbleJoystick()` works.
- **Init chain**: The host-side initialization may stall if Protocol Mode or required characteristics are missing. Ensure all HID Service characteristics are present.
- **Battery/Device Info**: Not in real SC2 firmware, but BlueZ HOGP driver requires them for `/dev/hidrawN` creation.

## Further Reading

| Document | Content |
|----------|---------|
| `docs/sc2-protocol.md` | SC2 BLE protocol details |
| `docs/att-server-implementation.md` | ATT protocol implementation |
| `research/triton_hid_analysis.md` | Firmware HID report construction |
| `research/triton_ble_state_analysis.md` | Firmware BLE stack + state machine |
| `research/ibex_command_table.md` | Complete 100-command table |
| `research/puck_esb_usb_analysis.md` | Puck dongle architecture (if spoofing via USB) |
