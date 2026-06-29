#!/usr/bin/env python3
"""Capture output reports sent to Neptune controller (/dev/hidraw3) by Steam.

Usage:
  1. Stop sc2-hogp service
  2. Run this script: sudo python3 /tmp/capture_haptics.py
  3. Start Steam on the Deck
  4. Click the trackpad
  5. Observe captured haptic reports
"""
import os, sys, time, struct, fcntl, select

HIDRAW_DEV = "/dev/hidraw3"

# HIDIOCGFEATURE - get feature report (from usbhid.h)
HIDIOCGFEATURE = 0x40402107

def main():
    print(f"[*] Opening {HIDRAW_DEV}...")
    try:
        fd = os.open(HIDRAW_DEV, os.O_RDWR)
    except PermissionError:
        print(f"[!] Permission denied. Run with sudo.")
        sys.exit(1)
    except FileNotFoundError:
        print(f"[!] {HIDRAW_DEV} not found. Check Neptune device.")
        sys.exit(1)

    print(f"[*] Opened {HIDRAW_DEV} (fd={fd})")
    print(f"[*] Waiting for output reports from Steam...")
    print(f"[*] Click the trackpad now!")
    print(f"[*] Press Ctrl+C to stop")
    print()

    # Read the device info
    buf = bytearray(256)
    try:
        # HIDIOCGRAWNAME - get device name
        name_buf = bytearray(256)
        fcntl.ioctl(fd, 0x40ff2106, name_buf)
        name = name_buf.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"[*] Device name: {name}")
    except:
        pass

    try:
        # HIDIOCGRAWPHYS - get device phys
        phys_buf = bytearray(256)
        fcntl.ioctl(fd, 0x40ff2107, phys_buf)
        phys = phys_buf.rstrip(b'\x00').decode('utf-8', errors='replace')
        print(f"[*] Device phys: {phys}")
    except:
        pass

    print()
    report_count = 0
    last_report_time = time.time()

    try:
        while True:
            # Poll for data with a timeout
            r, _, _ = select.select([fd], [], [], 0.1)

            if r:
                # Read output reports (input from device)
                try:
                    data = os.read(fd, 256)
                    if data:
                        report_count += 1
                        ts = time.time()
                        elapsed = ts - last_report_time
                        last_report_time = ts

                        print(f"--- Report #{report_count} ({elapsed:.3f}s since last) ---")
                        print(f"  Length: {len(data)} bytes")
                        print(f"  Hex:    {data.hex()}")

                        # Parse based on first byte
                        if len(data) >= 1:
                            first = data[0]
                            print(f"  Byte[0]: 0x{first:02x}")

                            # Check for known Neptune report types
                            if first == 0x09:
                                print(f"  Type: CONTROLLER_DECK_STATE (input report)")
                            elif first == 0xeb:
                                print(f"  Type: TriggerRumbleCommand (haptic output)")
                                if len(data) >= 9:
                                    left = struct.unpack_from('<H', data, 5)[0]
                                    right = struct.unpack_from('<H', data, 7)[0]
                                    print(f"  Left motor:  {left}")
                                    print(f"  Right motor: {right}")
                            elif first == 0x80:
                                print(f"  Type: SC2 Haptic Rumble (0x80)")
                            elif first == 0x81:
                                print(f"  Type: SC2 ClearDigitalMappings (0x81)")
                            elif first == 0x87:
                                print(f"  Type: SC2 SET_SETTINGS (0x87)")
                            elif first == 0x83:
                                print(f"  Type: SC2 GET_ATTRIBUTES (0x83)")
                            elif first == 0xae:
                                print(f"  Type: SC2 GET_SERIAL (0xae)")
                            else:
                                print(f"  Type: Unknown (0x{first:02x})")

                        print()
                except OSError as e:
                    print(f"[!] Read error: {e}")

    except KeyboardInterrupt:
        print(f"\n[*] Stopped. Captured {report_count} reports.")
    finally:
        os.close(fd)

if __name__ == "__main__":
    main()
