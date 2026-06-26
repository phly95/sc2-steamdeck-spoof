/*
 * HID Write Failure Analysis — Why vtable[0x10] Fails
 *
 * Binary: ~/.steam/debian-installation/linux64/steamclient.so
 * Status: DETERMINED
 */

/*
 * === EXECUTIVE SUMMARY ===
 *
 * The vtable[0x10] call at 0x010d4e14 does NOT actually fail in the
 * traditional sense. The issue is that [r15+0x208] == 0, which causes
 * the entire vtable dispatch to be SKIPPED. The feature report is
 * never sent. The "retry" is the state machine trying again later,
 * but the root cause is that the HID device connection was never
 * fully established for feature report writes.
 */

/*
 * === THE vtable[0x10] DISPATCH ===
 *
 * At 0x010d4e14:
 *
 *   0x010d4dfc: mov rax, [r15+0xa8]     ; load HID device array
 *   0x010d4e08: mov rax, [rax+rdx*8]    ; index into array
 *   0x010d4e0e: mov rdi, [rax]          ; load object pointer
 *   0x010d4e11: mov rax, [rdi]          ; load vtable
 *   0x010d4e14: call [rax+0x10]         ; dispatch vtable[0x10]
 *
 * The vtable method at 0x017605b0 is:
 *   mov [rdi+0x20], rsi    ; store context pointer
 *   ret
 *
 * This is a TRIVIAL SETTER — it cannot "fail" in isolation.
 * It stores a context pointer and returns void.
 */

/*
 * === THE REAL PROBLEM: [r15+0x208] == 0 ===
 *
 * Before reaching 0x010d4e14, the code checks [r15+0x208]:
 *
 *   0x010d4da0: cmp byte [r15+0x208], 0
 *   0x010d4db0: je 0x10d4fd0            ; if flag==0, SKIP vtable dispatch
 *
 * When [r15+0x208] == 0:
 *   - The vtable dispatch at 0x010d4e14 is SKIPPED
 *   - Code jumps to 0x10d4fd0 (comparison path)
 *   - No feature report is sent
 *   - The setting remains in the array → retried later
 *
 * [r15+0x208] is only set to 1 at 0x0156781c (YieldingRunTestProgram).
 * In normal operation, it's always 0.
 */

/*
 * === THE FEATURE REPORT PATH ===
 *
 * The actual HID write goes through:
 *
 * 1. CWriteFeatureReportWorkItem (RTTI at 0x00aa1880)
 * 2. CHIDMessageToRemote.DeviceSendFeatureReport (protobuf at 0x00b3ec09)
 * 3. IPC pipe (hiddevicepipesteam.cpp at 0x00c8ce9a)
 * 4. CHIDIOThread processes the write (0x00d6fbc2)
 * 5. SDL_hid_send_feature_report (resolved via dlsym at 0x01760fa2)
 *
 * The vtable at 0x02c69a10 has send_feature_report at offset 0x18.
 * The function is resolved dynamically at startup.
 */

/*
 * === WHY [r15+0x208] IS 0 ===
 *
 * The flag [r15+0x208] is set at 0x0156781c:
 *
 *   0x0156781c: mov byte [r15+0x208], 1
 *   0x0156782a: call 0x2844a00          ; StartRetryTimer
 *   0x01567847: lea rsi, "YieldingRunTestProgram"
 *
 * This is in a TEST/INITIALIZATION path. In normal controller
 * operation, this path is NOT taken. The flag stays 0.
 *
 * The flag is cleared at 0x0119f3b1:
 *   0x0119f3b1: mov byte [rdi+0x208], 0
 *
 * This is in a cleanup function that calls vtable[0x228].
 *
 * CONCLUSION: [r15+0x208] is a "test mode" flag. When it's 0,
 * the state machine skips the actual HID write and falls through
 * to a comparison path that either no-ops or calls an alternate
 * dispatch at 0x010d5260.
 */

/*
 * === THE BUFFER SIZE CHECK ===
 *
 * After the vtable call (if it happens), there's a buffer check:
 *
 *   0x010d4e49: mov edi, [r15+0xe4]    ; current buffer index
 *   0x010d4e50: lea edx, [rdi+1]       ; index + 1
 *   0x010d4e53: cmp edx, eax           ; eax = [r15+0xb8] (max)
 *   0x010d4e55: jl 0x10d50f8           ; if fits, continue
 *   0x010d4e5b: mov dword [r15+0xe4], 0 ; OVERFLOW: reset
 *   0x010d4e66: mov r14d, 1             ; set error flag
 *
 * This checks if the feature report data fits in the IPC pipe buffer.
 * If it doesn't, the buffer is reset and an error is flagged.
 *
 * However, this check is ONLY reached if the vtable dispatch at
 * 0x010d4e14 actually executes. When [r15+0x208]==0, the dispatch
 * is skipped, so this check is also skipped.
 */

/*
 * === IPC PATH DETAILS ===
 *
 * The feature report goes through:
 *
 * 1. CHIDMessageToRemote.DeviceSendFeatureReport (protobuf)
 *    - Field 1: device (uint32)
 *    - Field 2: data (bytes) — the 64-byte feature report
 *
 * 2. IPC pipe via hiddevicepipesteam.cpp
 *    - Source: /data/src/common/hiddevicepipesteam.cpp
 *    - The pipe connects steamclient.so to the HID I/O thread
 *
 * 3. CHIDIOThread processes the message
 *    - "CSteamController::CHIDIOThread" at 0x00d6fbc2
 *    - "CSteamController::CHIDIOThread::CWorkItemThread" at 0x00d73b6a
 *
 * 4. SDL_hid_send_feature_report is called
 *    - Resolved via dlsym at startup (0x01760fa2)
 *    - Stored at 0x02c69a28 (vtable + 0x18)
 *    - The actual SDL function pointer
 *
 * 5. BlueZ/bluetoothd receives the write
 *    - For BLE: ATT Write Request (0x12) or Write Command (0x52)
 *    - For USB: hidraw write
 *    - For Dongle: ESB protocol
 */

/*
 * === ERROR STRINGS ===
 *
 * 0x00d23fac: "Error uploading firmware. Failed to write feature report"
 * 0x00ce027a: "FWU Send complete feature report failed"
 * 0x00d02f59: "Update start cmd write feature report failed"
 * 0x00d40ec2: "Erase all write feature report failed"
 * 0x00d61bbb: "Erase page write feature report failed"
 *
 * All error strings are in FIRMWARE UPDATE paths, not SET_SETTINGS.
 * There is no error string for SET_SETTINGS write failure.
 */

/*
 * === BINARY REFERENCES ===
 *
 * vtable[0x10] dispatch: 0x010d4e14
 * vtable method: 0x017605b0 (trivial setter)
 * [r15+0x208] check: 0x010d4da0
 * [r15+0x208] set: 0x0156781c
 * [r15+0x208] cleared: 0x0119f3b1
 * SDL_hid_send_feature_report string: 0x00cbb561
 * dlsym resolution: 0x01760fa2
 * vtable storage: 0x02c69a28
 * CHIDMessageToRemote: 0x00b3ec09
 * IPC pipe source: 0x00c8ce9a (hiddevicepipesteam.cpp)
 * CHIDIOThread: 0x00d6fbc2
 * CWriteFeatureReportWorkItem RTTI: 0x00aa1880
 */
