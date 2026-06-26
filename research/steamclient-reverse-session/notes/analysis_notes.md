# Detailed Analysis Notes

## Binary Analysis Approach

### Tools Used
- **radare2 5.5.0**: String analysis, cross-reference finding
- **objdump**: Disassembly of specific functions
- **Python**: Custom scripts for binary pattern matching

### Key Addresses

| Address | Function/Reference |
|---------|-------------------|
| `0x01071c70` | EYldWaitForControllerDetails |
| `0x01092820` | QueueFetchingControllerDetails |
| `0x010c4a00` | Controller identification |
| `0x010c4a85` | "Controller uses V1 HID protocol" xref |
| `0x010c4e0c` | "Controller uses V1 HID protocol via BLE" xref |
| `0x00d4da02` | "EYldWaitForControllerDetails" string |
| `0x00c8a7f0` | "QueueFetchingControllerDetails" string |

### String Analysis

Found key strings in `.rodata.str` section:

```
0x00d4da02: EYldWaitForControllerDetails
0x00c8a7f0: QueueFetchingControllerDetails
0x00cef4d0: Controller uses V1 HID protocol
0x00d30ce0: Controller uses V1 HID protocol via BLE
0x00b3ebe6: device_send_feature_report
0x00b3ec3a: device_get_feature_report
0x00b3ed8b: device_start_input_reports
0x00ca6b56: toggle_lizard
```

### Cross-Reference Finding

Used RIP-relative LEA instruction pattern matching:
- Pattern: `48 8d XX 05 [disp32]` where XX & 0xc7 == 0x05
- This finds `lea reg, [rip + displacement]` instructions that reference string addresses

### Section Layout

```
.rodata:       vaddr=0x00a67a40, offset=0x00a67a40, size=0x219ff0
.rodata.str:   vaddr=0x00c84630, offset=0x00c84630, size=0x105310
.text:         vaddr=0x00d8d000, offset=0x00d8c000, size=~0x1e7ff00
```

Note: `.text` section has vaddr-offset difference of 0x1000 (typical for PIE binaries).

### ControllerDetails_tE Struct Analysis

From the copy patterns in `EYldWaitForControllerDetails` and `QueueFetchingControllerDetails`:

**Copy sequence (qword operations):**
```asm
mov rax, [rbp+0x20]    ; offset 0x20 from stack buffer
mov [rbx], rax          ; offset 0x00 in output
mov rax, [rbp+0x28]    ; offset 0x28
mov [rbx+0x8], rax     ; offset 0x08
; ... continues for 10 qwords ...
mov eax, [rbp+0x70]    ; offset 0x70
mov [rbx+0x50], eax    ; offset 0x50 (dword)
```

This indicates the struct is 0x54 bytes (84 bytes) with:
- 10 qword fields (80 bytes)
- 1 dword field (4 bytes)

### Ready Flag Mechanism

The critical instruction that unblocks registration:

```asm
; At 0x010929bf:
mov dword [r15 + 0x3c], 1    ; Set ready_flag = 1
```

This is the LAST instruction before the function returns, meaning:
1. All controller details are populated first
2. Then the ready flag is set
3. This unblocks the yield function
4. Registration completes

### Timeout Analysis

The timeout value `0x1e8480` = 2,000,000 microseconds = 2 seconds is used in:

```asm
; At 0x0107e24c:
mov edx, 0x1e8480    ; Timeout in microseconds
mov rdi, rbp         ; Buffer
lea rsi, [rip+...]   ; "EYldWaitForControllerDetails" string
call function_27aa510
```

This suggests the wait function:
1. Takes a timeout parameter (2 seconds)
2. Uses the string for logging/debugging
3. Returns 1 on success, 2 on timeout, other on error

### Product ID References

Found in controller identification code:

```asm
; At 0x010c4b38:
cmp eax, 0x1303    ; SC2 BLE product ID
je create_sc2_handler

; At 0x010c46f8:
cmp eax, 0x1302    ; SC2 USB product ID
```

### 0xf2 Command Dispatch

Multiple `cmp al, 0xf2` instructions found, indicating a switch-case dispatch:

```asm
cmp al, 0xf2    ; Check if command is 0xf2
je handle_f2    ; Jump to handler
```

The most promising handler candidates:
- `0x0124ad88` - In a larger function (start: `0x01241ba0`)
- `0x013013c1` - In a function (start: `0x012f8420`)
- `0x01393d40` - In a function (start: `0x0138d0f0`)

### SET_SETTINGS Command Pattern

The SET_SETTINGS command (0x87) appears in multiple locations:

```asm
mov al, 0x87    ; SET_SETTINGS command
```

Found at:
- `0x010d544c` - Initial SET_SETTINGS
- `0x014fd614`, `0x014fd620` - Retry path 1
- `0x014fdf44`, `0x014fdf50` - Retry path 2

The repeated pattern suggests a retry mechanism with multiple attempts.

### Haptic Work Items

Found work item classes for haptic effects:

```
CPulseHapticWorkItem        - Single pulse
CSimpleHapticTickWorkItem   - Simple tick
CHapticToneWorkItem         - Tone pattern
CLegacySimpleHapticWorkItem - Legacy compatibility
CHapticScriptWorkItem       - Scripted sequences
```

These are queued to `CRumbleThread` for processing.

---

## Validation Against Observed Behavior

### 1. SET_SETTINGS 0x09 Retry
- **Observed**: Steam retries every 3 seconds
- **Binary**: Multiple `mov al, 0x87` in retry paths
- **Consistent**: ✓

### 2. 0xf2 Sent 8 Times
- **Observed**: 0xf2 command sent 8 times during handshake
- **Binary**: Multiple `cmp al, 0xf2` dispatch points
- **Consistent**: ✓ (different categories/sub-commands)

### 3. All Test Results FAIL
- **Observed**: All controller tests fail (triggers, trackpads, etc.)
- **Binary**: Registration blocked at EYldWaitForControllerDetails
- **Consistent**: ✓ (if our spoof doesn't set ready_flag)

### 4. Newer FW Shows Partial Success
- **Observed**: SC2 #2 with newer FW shows some PASS results
- **Binary**: Different capability responses for different FW versions
- **Consistent**: ✓ (newer FW may send different 0xf2 responses)

---

*Analysis completed: 2026-06-25*
*Analyst: opencode reverse engineering session*
