/*
 * Retry Mechanism Analysis — How SET_SETTINGS Retries Work
 *
 * Binary: ~/.steam/debian-installation/linux64/steamclient.so
 * Status: DETERMINED
 */

⚠️ DISCLAIMER: WRONG BINARY ANALYZED

All analysis in this file was performed on the WRONG binary:
  ~/.steam/debian-installation/linux64/steamclient.so (46MB, 64-bit x86_64)

Steam actually loads:
  ~/.steam/debian-installation/ubuntu12_32/steamclient.so (49MB, 32-bit i386)

ALL ADDRESSES, FUNCTION OFFSETS, AND DISASSEMBLY ARE WRONG for the running process.
The conceptual findings (gate mechanism, YieldingRunTestProgram, job system) likely
apply to both binaries, but every specific address must be re-derived from the
32-bit binary or verified via GDB on the running process.

Verified: 2026-06-29
- Steam process: ELF 32-bit LSB pie executable (i386)
- steamclient.so loaded: ubuntu12_32/steamclient.so
- YieldingRunTestProgram string: 0x00bfc7e3 (32-bit) vs 0x00d6d17b (64-bit)


/*
 * === EXECUTIVE SUMMARY ===
 *
 * The SET_SETTINGS retry mechanism works as follows:
 *
 * 1. Settings are queued as 16-byte entries in array at [r15+0xc0]
 * 2. The state machine iterates through entries
 * 3. For each entry, it attempts to send via vtable[0x10]
 * 4. If [r15+0x208]==0, the send is SKIPPED (no actual HID write)
 * 5. The entry remains in the array
 * 6. The state machine re-processes on next iteration (~3 seconds)
 * 7. No retry count limit — retries indefinitely until success
 *
 * The "failure" is not a traditional error. It's the state machine
 * skipping the send because the HID device connection wasn't
 * established (flag at [r15+0x208] is 0).
 */

/*
 * === RETRY FLOW ===
 *
 * 1. ENTRY: State machine at 0x010d466b processes settings
 *
 * 2. CHECK: 0x010d4da0 — cmp byte [r15+0x208], 0
 *    - If 0: skip to comparison path (0x10d4fd0)
 *    - If 1: proceed to send path (0x10d4dc6)
 *
 * 3. SEND: 0x010d4e14 — call [rax+0x10] (vtable dispatch)
 *    - If [r15+0x208]==0: SKIPPED (jump to 0x10d4fd0)
 *    - If [r15+0x208]==1: EXECUTED (trivial setter)
 *
 * 4. RETURN: 0x010d4e17 — test r12b, r12b
 *    - r12b is the enable flag (callee-saved, not modified by call)
 *    - Tests whether we were enabling or disabling
 *    - NOT testing the return value of vtable[0x10]
 *
 * 5. ADVANCE: 0x010d4e49-0x010d4e55 — check index
 *    - mov edi, [r15+0xe4] (current index)
 *    - lea edx, [rdi+1] (next index)
 *    - cmp edx, eax (compare with max count [r15+0xb8])
 *    - jl 0x10d50f8 (if more settings, save index and continue)
 *    - mov [r15+0xe4], 0 (reset index if done)
 *
 * 6. RETRY: The loop continues via 0x10d4cc2
 *    - Re-reads settings array
 *    - Processes next entry
 *    - If [r15+0xd0] > 0, loops back
 *
 * 7. TIMER: The 3-second interval (0x2DC6C0 µs) at 0x01f84301
 *    - Controls how often the state machine runs
 *    - Not a "retry timer" — it's the polling interval
 */

/*
 * === SETTINGS COUNT MANAGEMENT ===
 *
 * The settings count at [r15+0xd0] is:
 *   - INCREMENTED when entries are queued (0x010d550b)
 *   - DECREMENTED when entries are processed (0x010d4f1c)
 *   - CHECKED to determine if more work exists (0x010d4c96)
 *
 * Decrement logic:
 *   0x010d4f01: mov eax, [r15+0xd0]   ; load count
 *   0x010d4f08: mov edx, eax           ; edx = count
 *   0x010d4f0a: sub edx, ebp           ; edx -= index
 *   0x010d4f0c: sub edx, 1             ; edx -= 1
 *   0x010d4f0f: test edx, edx          ; check if positive
 *   0x010d4f11: jg 0x10d4fa0           ; if more settings, continue
 *   0x010d4f17: sub eax, 1             ; count--
 *   0x010d4f1c: mov [r15+0xd0], eax    ; store updated count
 *
 * The count is only decremented when a setting is SUCCESSFULLY
 * processed (sent or confirmed as already applied).
 */

/*
 * === PENDING FLAG [r15+0x208] ===
 *
 * Set to 1 at: 0x0156781c (YieldingRunTestProgram initialization)
 * Cleared at: 0x0119f3b1 (after calling vtable[0x228])
 *
 * In normal operation:
 *   - [r15+0x208] is ALWAYS 0
 *   - The vtable dispatch at 0x010d4e14 is ALWAYS SKIPPED
 *   - Settings are NEVER actually sent via HID
 *   - The state machine keeps "retrying" but never succeeds
 *
 * This means SET_SETTINGS 0x87 (lizard mode) is NEVER applied
 * in normal operation. The controller stays in whatever mode it
 * was in before the retry loop started.
 */

/*
 * === NO RETRY COUNT LIMIT ===
 *
 * There is no explicit retry count or maximum retry attempts.
 * The state machine continues indefinitely:
 *
 *   while (count > 0) {
 *       process_entry(index);
 *       if (index < max) index++;
 *       else index = 0;
 *   }
 *
 * The only way the loop exits is if:
 *   - count reaches 0 (all settings processed)
 *   - The controller disconnects
 *   - The state machine is shut down
 *
 * Since settings are never "consumed" (count never decreases
 * because sends are skipped), the loop runs forever.
 */

/*
 * === DOES RETRY BLOCK OTHER OPERATIONS? ===
 *
 * NO. The state machine runs in a worker thread (CHIDIOThread).
 * Other operations (GET_ATTRIBUTES, registration, etc.) run on
 * different threads or in different execution contexts.
 *
 * The SET_SETTINGS retry is background noise that doesn't
 * prevent other operations from completing.
 */

/*
 * === BINARY REFERENCES ===
 *
 * State machine: 0x010d466b
 * Settings count: [r15+0xd0]
 * Settings array: [r15+0xc0]
 * Current index: [r15+0xe4]
 * Max count: [r15+0xb8]
 * Pending flag: [r15+0x208]
 * Send dispatch: 0x010d4e14
 * Index advance: 0x010d4e49-0x010d4e55
 * Count decrement: 0x010d4f1c
 * Timer: 0x01f84301 (0x2DC6C0 = 3 seconds)
 * CHIDIOThread: 0x00d6fbc2
 */
