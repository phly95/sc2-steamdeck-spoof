# Steam Controller 2026 BLE Protocol

## Device Identification

| Mode | VID | PID | Product Name |
|------|-----|-----|--------------|
| BLE | 0x28DE | 0x1303 | Steam Controller 2026 |
| USB (wired) | 0x28DE | 0x1302 | Steam Controller 2026 |
| Puck (dongle) | 0x28DE | 0x1304 | Steam Controller Puck |

- **Manufacturer**: Valve Software
- **Vendor ID**: 0x28DE (Valve)
- **PnP ID PID**: 0x0003

## GATT Service UUID

```
100F6C32-1735-4313-B402-38567131E5F3
```

This is the primary HID service UUID for the Steam Controller 2026 BLE profile. It is NOT the standard HID service (0x1812) — it is a Valve custom UUID.

## Characteristics

| Characteristic | UUID | Properties | Description |
|---------------|------|------------|-------------|
| Input Report 1 | `100F6C7A-...` | Read, Notify | Input report (report ID 0x45) |
| Input Report 2 | `100F6C7C-...` | Read, Notify | Input report (report ID 0x47) |
| Report | `100F6C34-1735-4313-B402-38567131E5F3` | Read, Write, Write Without Response | Output/feature report |

## Input Report Formats

### Report 0x45 (45 bytes) — Primary Input

**Note**: Report ID (0x45) is NOT included in the ATT notification payload — BlueZ
hog-ll adds it from the Report Reference descriptor. The notification data is exactly
45 bytes starting at offset 0 = sequence number.

**Verified against btmon captures from real SC2 device (serial 28de-1303-2efea7d).**

```
Offset  Size  Description
0       1     Sequence number (incrementing by 0x32 per report)
1       4     Button state (32-bit bitmask, little-endian)
5       1     Left trigger (0-255)
6       1     Right trigger (0-255)
7       2     Left stick X (signed 16-bit LE, ~1050 at center)
9       2     Left stick Y (signed 16-bit LE, ~-1900 at center)
11      2     Right stick X (signed 16-bit LE, ~450 at center)
13      2     Right stick Y (signed 16-bit LE, ~1250 at center)
15      2     Left trackpad X (signed 16-bit LE)
17      2     Left trackpad Y (signed 16-bit LE)
19      2     Right trackpad X (signed 16-bit LE)
21      2     Right trackpad Y (signed 16-bit LE)
23      2     IMU accelerometer X (signed 16-bit LE)
25      2     IMU accelerometer Y (signed 16-bit LE)
27      2     IMU accelerometer Z (signed 16-bit LE, ~16400 = 1g)
29      2     IMU gyroscope X (signed 16-bit LE)
31      2     IMU gyroscope Y (signed 16-bit LE)
33      2     IMU gyroscope Z (signed 16-bit LE)
35      4     IMU timestamp (32-bit LE, microseconds)
39      6     Reserved (quaternion W/X/Y — zeros in 45-byte report)
```

**Total: 45 bytes**

Note: The 45-byte report does NOT include quaternion Z or full quaternion data.
The full 48-byte format (with Report ID + quaternion) is used over USB (Report 0x42).

### Report 0x47 (47 bytes) — Extended Input

Same as 0x45 but adds 16-bit trackpad timestamp fields.

### Report 0x42 (53 bytes) — USB Input

Same as 0x45 but adds full quaternion (4 × 16-bit) to the IMU data.

## Button Bitmask (32-bit)

**Verified from SDL3 `SDL_hidapi_steam_triton.c` TritonButtons enum.**

```
Bit 0:  A
Bit 1:  B
Bit 2:  X
Bit 3:  Y
Bit 4:  QAM (Quick Access Menu / ...)
Bit 5:  R3 (Right Stick Click)
Bit 6:  View (Options / Select)
Bit 7:  R4 (Right Paddle 1)
Bit 8:  R5 (Right Paddle 2)
Bit 9:  R (Right Bumper)
Bit 10: D-Pad Down
Bit 11: D-Pad Right
Bit 12: D-Pad Left
Bit 13: D-Pad Up
Bit 14: Menu
Bit 15: L3 (Left Stick Click)
Bit 16: Steam
Bit 17: L4 (Left Paddle 1)
Bit 18: L5 (Left Paddle 2)
Bit 19: L (Left Bumper)
Bit 20: Right Joystick Touch (capacitive)
Bit 21: Right Trackpad Touch
Bit 22: Right Trackpad Click
Bit 23: Right Trigger Click (binary threshold)
Bit 24: Left Joystick Touch (capacitive)
Bit 25: Left Trackpad Touch
Bit 26: Left Trackpad Click
Bit 27: Left Trigger Click (binary threshold)
Bit 28: Right Grip Touch (capacitive)
Bit 29: Left Grip Touch (capacitive)
Bit 30-31: Reserved
```

## HID Descriptor (Vendor Interface)

```
Usage Page (0xFF00)        ; Vendor-defined
Usage (0x01)               ; Vendor usage
Collection (Application)
  Report ID (0x00)
  Report Size (8)
  Report Count (64)
  Input (Data, Var, Abs)   ; 64-byte vendor input report
  Report ID (0x00)
  Report Size (8)
  Report Count (64)
  Output (Data, Var, Abs)  ; 64-byte vendor output report
End Collection
```

The vendor HID interface uses Usage Page 0xFF00 with 64-byte I/O reports. This is used for raw vendor communication (firmware updates, feature reports).

## Mode Switching

The SC2 starts in **Lizard Mode** (basic keyboard/mouse emulation):
- Left trackpad: mouse movement
- Right trackpad: scroll
- Buttons: keyboard keys
- Triggers: left/right mouse buttons

To switch to **Steam Input Mode** (full controller input):
1. Steam Client sends a feature report to the device
2. Device switches from lizard mode to Steam Input mode
3. Device begins sending full controller input reports

Key strings from Steam Client:
- `toggle_lizard` — toggle lizard mode
- `is_mode_switching_supported` — check if mode switching is available
- `CExitLizardModeWorkItem` — work item to exit lizard mode

## Feature Reports

| Report ID | Direction | Description |
|-----------|-----------|-------------|
| 0x00 | Device ↔ Host | SC2 command channel (GET_ATTRIBUTES, SET_SETTINGS, etc.) |
| 0x01 | Device → Host | Device capabilities |
| 0x85 | Host → Device | Mode switch (lizard/Steam Input) |
| 0x86 | Host → Device | Configuration |
| 0x87 | Host → Device | Calibration data |

## SC2 Command Bytes (Feature Report 0x00)

| Byte | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x80 | ID_SET_DIGITAL_MAPPINGS | Host→Device | Set button mappings |
| 0x81 | ID_CLEAR_DIGITAL_MAPPINGS | Host→Device | Clear mappings (exit lizard mode) |
| 0x82 | ID_GET_DIGITAL_MAPPINGS | Host→Device | Get current mappings |
| 0x83 | ID_GET_ATTRIBUTES_VALUES | Bidirectional | Get device attributes |
| 0x84 | ID_GET_ATTRIBUTE_LABEL | Host→Device | Get attribute label |
| 0x85 | ID_SET_DEFAULT_DIGITAL_MAPPINGS | Host→Device | Set default mappings (enter gamepad mode) |
| 0x86 | ID_FACTORY_RESET | Host→Device | Factory reset |
| 0x87 | ID_SET_SETTINGS_VALUES | Host→Device | Set controller settings |
| 0x88 | ID_CLEAR_SETTINGS_VALUES | Host→Device | Clear settings |
| 0x89 | ID_GET_SETTINGS_VALUES | Bidirectional | Get current settings |
| 0x8A | ID_GET_SETTING_LABEL | Host→Device | Get setting label |
| 0x8B | ID_GET_SETTINGS_MAXS | Host→Device | Get max values |
| 0x8C | ID_GET_SETTINGS_DEFAULTS | Host→Device | Get default values |
| 0x8D | ID_SET_CONTROLLER_MODE | Host→Device | Mode switch (lizard ↔ Steam Input) |
| 0x8E | ID_LOAD_DEFAULT_SETTINGS | Host→Device | Load default settings |
| 0x8F | ID_TRIGGER_HAPTIC_PULSE | Host→Device | Trigger haptic pulse |
| 0x9F | ID_TURN_OFF_CONTROLLER | Host→Device | Turn off controller |
| 0xA1 | ID_GET_DEVICE_INFO | Host→Device | Get device info |
| 0xAE | ID_GET_SERIAL | Bidirectional | Get serial number |
| 0xBA | ID_GET_CHIP_ID | Bidirectional | Get chip ID |
| 0xF2 | Unknown | Bidirectional | Per-category capability query |

## Settings Registers

| Register | Name | Values |
|----------|------|--------|
| 0 | SETTING_MOUSE_SENSITIVITY | |
| 1 | SETTING_MOUSE_ACCELERATION | |
| 2 | SETTING_TRACKBALL_ROTATION_ANGLE | |
| 3 | SETTING_HAPTIC_INTENSITY_UNUSED | |
| 4 | SETTING_LEFT_GAMEPAD_STICK_ENABLED | |
| 5 | SETTING_RIGHT_GAMEPAD_STICK_ENABLED | |
| 6 | SETTING_USB_DEBUG_MODE | |
| 7 | SETTING_LEFT_TRACKPAD_MODE | 7=None |
| 8 | SETTING_RIGHT_TRACKPAD_MODE | 7=None |
| 9 | SETTING_LIZARD_MODE | 0=OFF, 1=ON |
| 10 | SETTING_DPAD_DEADZONE | |
| 15 | SETTING_HAPTIC_INCREMENT | |
| 21 | SETTING_SENSITIVITY_SCALE_AMOUNT | |
| 24 | SETTING_SMOOTH_ABSOLUTE_MOUSE | |
| 48 | SETTING_IMU_MODE | |
| 70 | SETTING_HAPTICS_ENABLED | |
| 79 | SETTING_HAPTIC_INTENSITY | |

### GET_ATTRIBUTES (0x83) Response Format

The response contains 9 attributes, each as 1-byte tag + 4-byte LE uint32 value:

| Tag | Name | Real SC2 Value | Description |
|-----|------|----------------|-------------|
| 1 | ATTRIB_PRODUCT_ID | 0x1303 | SC2 BLE Product ID |
| 2 | ATTRIB_CAPABILITIES | 0x4169bfff | Feature capabilities bitmask |
| 4 | ATTRIB_FIRMWARE_BUILD_TIME | 0x65E4F1AD | Firmware build timestamp |
| 9 | ATTRIB_BOARD_REVISION | 46 | Hardware board revision |
| 10 | ATTRIB_BOOTLOADER_BUILD_TIME | varies | Bootloader build timestamp |
| 11 | ATTRIB_CONNECTION_INTERVAL_IN_US | 4000 | BLE connection interval |
| 12 | ATTRIB_12 | 0 | Unknown |
| 13 | ATTRIB_13 | 0 | Unknown |
| 14 | ATTRIB_14 | 0 | Unknown |

### Capabilities Bitmask (0x4169bfff)

```
Bit 0-9:   Buttons (A, B, X, Y, QAM, R3, View, R4, R5, R)
Bit 10-19: Triggers, D-Pad, Menu, L3, Steam, L4, L5, L
Bit 20-25: Joystick touch, Trackpad touch/click
Bit 26-29: Trigger clicks, Grip touch
Bit 30-31: IMU
Bit 37:    Haptics
Bit 39:    Dual trackpads
```

## Pairing

The SC2 uses standard BLE pairing:
1. Device advertises with SC2-specific service UUID
2. Host scans and connects
3. Pairs using Just Works or Passkey
4. Discovers GATT services
5. Subscribes to input report notifications
6. Sends feature report to switch out of lizard mode
7. Begins receiving controller input

## Comparison with Steam Deck (Neptune)

The Steam Deck controller ("Neptune") uses:
- VID: 0x28DE
- PID: 0x1205
- 5 USB interfaces (mouse, keyboard, vendor HID, audio, CDC)
- Separate motion sensor input device
- Virtual Xbox 360 pad via hid-steam driver

The SC2 BLE profile is significantly simpler, using a single GATT service with characteristics for input/output.
