/*
 * Connection Type Detection — COMPLETE ANALYSIS
 *
 * Binary: ~/.steam/debian-installation/linux64/steamclient.so
 * Status: DETERMINED
 */

/*
 * === EXECUTIVE SUMMARY ===
 *
 * Steam determines connection type (BLE, USB, Dongle) via:
 * 1. Product ID dispatch at 0x010c4a00 (primary method)
 * 2. Connection type bitfield at controller+0x180 (runtime state)
 * 3. Protobuf transport enum (wireless_transport field)
 *
 * The product ID determines the transport at handler creation time.
 * The bitfield tracks active transport connections at runtime.
 */

/*
 * === METHOD 1: PRODUCT ID DISPATCH (0x010c4a00) ===
 *
 * The function at 0x010c4a00 is the main HID protocol dispatch.
 * It reads the product ID from [r12+0x3c] and routes to the
 * appropriate handler path.
 *
 * Flow:
 *   r12 → handler object (48 bytes)
 *   eax ← [r12+0x3c] (product ID)
 *
 *   cmp eax, 0x1106     → 0x010c4c30 (upper branch)
 *   cmp eax, 0x1104     → 0x010c4734
 *   cmp eax, 0x1042     → 0x010c4a59 (generic V1 HID)
 *   sub eax, 0x1101; cmp eax, 1 → ja 0x010c4b51 (unrecognized)
 *   cmp eax, 0x1303     → 0x010c4de0 (BLE path!)
 *   sub eax, 0x1304; cmp eax, 1 → jbe 0x010c4c40 (dongle path)
 *
 * Product ID → Transport → Handler Path:
 *
 *   0x1002-0x1004  USB         0x010c4a59
 *   0x1042         Generic     0x010c4a59
 *   0x1101-0x1102  Generic     0x010c4a59
 *   0x1104         ?           0x010c4734
 *   0x1106         ?           0x010c4c30
 *   0x1142         Generic     0x010c4a59
 *   0x1220         USB         0x010c4940
 *   0x1303         BLE         0x010c4de0 → 0x010c4e0c
 *   0x1304-0x1305  Dongle      0x010c4c40
 *   0x28de         ?           0x010c48b6 → 0x010c46ec
 */

/*
 * === METHOD 2: BLE FLAG IN HANDLER OBJECT ===
 *
 * Each transport path sets a flag at handler+0x08:
 *
 * BLE path (0x010c4e71):
 *   mov byte [r12 + 8], 1     ; BLE = 1
 *
 * Dongle path (0x010c4c40):
 *   mov byte [r12 + 8], 0     ; BLE = 0
 *
 * USB path (0x010c4940):
 *   mov byte [r12 + 8], 0     ; BLE = 0
 *
 * This flag is a metadata marker used throughout the code to
 * conditionally execute BLE-specific logic (e.g., different
 * initialization sequences, bond management, timing).
 */

/*
 * === METHOD 3: CONNECTION TYPE BITFIELD (controller+0x180) ===
 *
 * At runtime, the connection type is stored as a bitfield at
 * controller offset 0x180. This is loaded and checked in the
 * function at 0x0111b180:
 *
 *   mov r13, [rdi+0x180]     ; load connection type bitfield
 *   test r13, r13
 *   je 0x111b340              ; if null, skip
 *
 * A jump table at 0x00aa5ab4 dispatches based on individual bit checks:
 *
 *   Bit  Shift  Location      Possible Meaning
 *   0    shr 0  0x0111b2f0    transport type A
 *   1    shr 1  0x0111b300    transport type B
 *   2    shr 2  0x0111b2d0    transport type C
 *   3    shr 3  0x0111b310    transport type D
 *   5    shr 5  0x0111b2b0    transport type E
 *   11   shr 0xb 0x0111b330   transport type F
 *   12   shr 0xc 0x0111b2c0   transport type G
 *   24   shr 0x18 0x0111b208  transport type H
 *   25   shr 0x19 0x0111b320  transport type I
 *   39   shr 0x27 0x0111b1d4  "wired" check → stored at [rsp+0xf]
 *
 * The bit 39 check (wired) is used to determine if the controller
 * is connected via a wired (USB) connection vs wireless (BLE/Dongle).
 */

/*
 * === METHOD 4: PROTOBUF TRANSPORT ENUM ===
 *
 * From the string table at 0x00a74076:
 *
 *   Value  Name           Description
 *   0      Triton_BL      Triton bootloader
 *   1      Proteus_BL     Proteus bootloader
 *   2      Triton_USB     Triton wired USB
 *   3      Triton_BLE     Triton Bluetooth LE
 *   4      Triton_ESB     Triton dongle (ESB)
 *   5      Proteus_USB    Proteus wired USB
 *   6      Nereid_USB     Nereid wired USB
 *
 * "Triton" = SC2 controller codename
 * "ESB" = Enhanced ShockBurst (Nordic's protocol for dongle)
 * "BL" = Bootloader
 * "Proteus" = Steam Controller 1 (original)
 * "Nereid" = Another controller variant
 *
 * The protobuf field "wireless_transport" at 0x00ae1435 encodes this
 * value in messages sent between Steam client and controller.
 */

/*
 * === HOW TRANSPORT AFFECTS HAPTICS ===
 *
 * 1. BLE (0x1303, Triton_BLE):
 *    - Haptics sent via ATT Write Command (0x52) to output report handle (0x0019)
 *    - BlueZ HOG profile handles the BLE communication
 *    - steamclient.so sends feature reports via IPC to bluetoothd
 *    - bluetoothd forwards via ATT operations
 *    - The set_report_cb() error (ATT error 0x0E) may indicate initialization issues
 *
 * 2. USB (0x1302, Triton_USB):
 *    - Haptics sent via SDL_hid_write() directly (no BLE stack)
 *    - steamclient.so opens /dev/hidrawN directly
 *    - Feature reports via SDL_hid_send_feature_report()
 *    - Output reports via SDL_hid_write()
 *
 * 3. Dongle (0x1304/0x1305, Triton_ESB):
 *    - Haptics sent via dongle protocol (ESB - Enhanced ShockBurst)
 *    - Similar to USB but through dongle's wireless interface
 *    - steamclient.so communicates with dongle via hidraw
 *    - Dongle forwards to controller via ESB
 *
 * KEY DIFFERENCE:
 * - BLE: steamclient.so → IPC → bluetoothd → ATT → controller
 * - USB: steamclient.so → SDL_hid_write() → /dev/hidrawN → controller
 * - Dongle: steamclient.so → SDL_hid_write() → dongle → ESB → controller
 */

/*
 * === BLE-SPECIFIC INITIALIZATION ===
 *
 * The BLE path at 0x010c4e0c sets up additional state:
 *
 *   0x010c4e56: mov edi, 0x30              ; sizeof = 48 bytes
 *   0x010c4e5f: call 0x2a6ca70             ; operator new
 *   0x010c4e71: mov byte [r12 + 8], 1     ; BLE flag = 1
 *   0x010c4e77: mov qword [r12], rax      ; vtable = 0x02ae1b58
 *   0x010c4e9d: call 0x2228880             ; string init (bond state?)
 *   0x010c4ea2: jmp 0x10c4a0e             ; common exit (vtable overwrite)
 *
 * The call to 0x2228880 after BLE handler creation may initialize
 * BLE-specific state (bond management, connection parameters, etc.).
 *
 * Related BLE strings:
 *   "tritoncontroller.cpp" at 0x00cbf534
 *   "triton bond state" at 0x00cc0a90
 *   "triton pair bond" at 0x00d0596b
 *   "Failed to read triton info from controller" at 0x00cc468f
 */

/*
 * === V1 HID PROTOCOL VARIANTS ===
 *
 * The binary recognizes 4 V1 HID protocol variants:
 *
 * 1. "Controller uses V1 HID protocol\n" (0x00cef4d0)
 *    - Generic USB/unknown controller
 *    - Handler: 0x010c4a59
 *
 * 2. "Controller uses V1 HID protocol via USB\n" (0x00cf1150)
 *    - Explicit USB connection
 *    - Handler: 0x010c4940
 *
 * 3. "Controller uses V1 HID protocol via Dongle\n" (0x00d216e0)
 *    - Dongle (ESB) connection
 *    - Handler: 0x010c4c40
 *
 * 4. "Controller uses V1 HID protocol via BLE\n" (0x00d30ce0)
 *    - Bluetooth LE connection
 *    - Handler: 0x010c4de0 → 0x010c4e0c
 *
 * 5. "Unrecognized controller using V1 HID protocol\n" (0x00cef4d0)
 *    - Unknown product ID
 *    - Handler: 0x010c4b51
 *
 * Additionally: "Controller uses V2 HID protocol" at 0x00cef00f
 *    - Newer protocol version (not used for SC2)
 */

/*
 * === BINARY REFERENCES ===
 *
 * Product ID dispatch: 0x010c4a00
 * BLE handler: 0x010c4de0 → 0x010c4e0c
 * Dongle handler: 0x010c4c40
 * USB handler: 0x010c4940
 * Generic handler: 0x010c4a59
 * Common exit: 0x010c4a0e
 * Connection bitfield: controller+0x180
 * Jump table: 0x00aa5ab4
 * BLE flag: handler+0x08
 * Initialized flag: handler+0x28
 *
 * String references:
 * "Controller uses V1 HID protocol via BLE" at 0x00d30ce0
 * "Controller uses V1 HID protocol via Dongle" at 0x00d216e0
 * "Controller uses V1 HID protocol via USB" at 0x00cf1150
 * "Controller uses V1 HID protocol" at 0x00cef4d0
 * "Unrecognized controller using V1 HID protocol" at 0x00cef4d0
 * "V2 HID protocol" at 0x00cef00f
 *
 * Protobuf transport enum: 0x00a74076
 * "wireless_transport" field: 0x00ae1435
 * "tritoncontroller.cpp": 0x00cbf534
 * "triton wireless protocol": 0x00d02687
 * "triton bond state": 0x00cc0a90
 * "triton pair bond": 0x00d0596b
 */
