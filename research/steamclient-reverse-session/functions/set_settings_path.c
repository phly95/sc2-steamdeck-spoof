/*
 * SET_SETTINGS Path Analysis — Does It Go Through the State Machine?
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
 * YES, SET_SETTINGS goes through the state machine at 0x010d466b.
 * But the verification step (vtable[0x130]) is SKIPPED because:
 *   - The verify object (r13) is NULL for SET_SETTINGS
 *   - The state machine checks `test r13, r13` at 0x010d4e6c
 *   - If r13==0, it jumps to 0x10d4ff1 (timing calc, no verify)
 *
 * The command byte (0x87) is stored at [r15+0xe0], not hardcoded.
 * The state machine is command-agnostic — it processes whatever
 * command is in [r15+0xe0].
 */

/*
 * === CORRECTED: 0x010d544c IS NOT mov al, 0x87 ===
 *
 * Previous sessions identified 0x010d544c as `mov al, 0x87`.
 * This is WRONG. The bytes at 0x010d544c are:
 *
 *   0x010d544b: e8 b0 87 5f 01    call 0x26cdc00    ; assertion()
 *   0x010d5450: 84 c0             test al, al
 *   0x010d5452: 0f 85 67 ff ff ff jne 0x10d53bf
 *
 * The 0x87 is part of the displacement bytes of `call 0x26cdc00`,
 * NOT an opcode being loaded. The `mov al, 0x87` is a disassembly
 * artifact from misaligned decoding.
 */

/*
 * === SET_SETTINGS QUEUE FUNCTION (0x010d5488) ===
 *
 * This function queues a setting entry to the settings array:
 *
 *   void QueueSetting(ControllerSettings* this, int count, double timestamp) {
 *       // Compute timestamp from this->frame_rate (this+0x118)
 *       float delay = this->frame_rate / CONSTANT;
 *       double computed_time = timestamp + delay;
 *
 *       // Push 16-byte entry to settings array
 *       int index = this->count;  // [this+0xd0]
 *       int new_count = index + 1;
 *
 *       if (new_count > this->capacity) {  // [this+0xc8]
 *           GrowArray(this);  // call 0x10d3080
 *       }
 *
 *       this->count = new_count;  // [this+0xd0] = new_count
 *
 *       // Store 16-byte entry: {flag=1, padding, timestamp}
 *       xmmword entry = { 1, 0, computed_time_lo, computed_time_hi };
 *       this->buffer[index * 16] = entry;  // [this+0xc0]
 *   }
 *
 * KEY: This function does NOT set [r15+0x208]. It only adds entries
 * to the settings array. The state machine processes these entries.
 */

/*
 * === STATE MACHINE FLOW (0x010d466b) ===
 *
 * The state machine processes settings at [r15+0xc0]:
 *
 *   void ProcessSettings(ControllerSettings* r15, callback r13, ...) {
 *       if (r15->init_flag [0xf0] == 0) {
 *           // First-time initialization
 *           if (r15->count [0xd0] <= 0) return;
 *           // Process settings...
 *       }
 *
 *       // Main loop
 *       for (int i = 0; i < r15->count; i++) {
 *           entry = &r15->buffer[i * 16];
 *           if (entry->flag == 0) continue;  // skip disabled
 *
 *           // Threshold checks...
 *
 *           // SEND (vtable[0x10])
 *           r15->send_state [0xe1] = send_result;
 *
 *           // VERIFY (vtable[0x130]) — ONLY if r13 != NULL
 *           if (r13 == NULL) {
 *               // Skip verify, go to timing calc
 *               goto timing_calc;
 *           }
 *           result = r13->vtable[0x130](r13, r15->report_id [0x198]);
 *           // Process result...
 *       }
 *   }
 */

/*
 * === THE CRITICAL BRANCH: test r13, r13 at 0x010d4e6c ===
 *
 *   0x010d4e6c: test r13, r13          ; check if verify object exists
 *   0x010d4e6f: je 0x10d4ff1           ; if NULL → SKIP VERIFY
 *
 *   0x010d4e75: mov rax, [r13]         ; (only reached if r13 != NULL)
 *   0x010d4e79: mov rdi, r13
 *   0x010d4e7c: mov esi, [r15+0x198]   ; report ID
 *   0x010d4e83: call [rax+0x130]        ; VERIFY (get_feature_report)
 *
 * For SET_SETTINGS: r13 is NULL → verify is SKIPPED
 * For GET_ATTRIBUTES: r13 is non-NULL → verify happens
 */

/*
 * === WHERE [r15+0x208] FITS IN ===
 *
 * The flag [r15+0x208] is a separate gatekeeper:
 *
 *   0x010d4da0: cmp byte [r15+0x208], 0
 *   0x010d4db0: je 0x10d4fd0           ; if flag==0, go to comparison path
 *
 * When [r15+0x208]==0 (normal operation):
 *   - Goes to comparison path at 0x10d4fd0
 *   - Reads byte from settings buffer, compares with [r15+0xe1]
 *   - If mismatch → sends (0x10d4dc6)
 *   - If match → skips to verify (0x10d4e75)
 *
 * When [r15+0x208]!=0 (test initialization only):
 *   - Always sends (the xor/cmp at 0x010d4db9-0x010d4dc0 is dead code)
 *
 * KEY: [r15+0x208] is only set to 1 during YieldingRunTestProgram
 * (0x0156781c). In normal operation, it's always 0.
 */

/*
 * === CExitLizardModeWorkItem ===
 *
 * RTTI string at 0x00aa19e0: "23CExitLizardModeWorkItem"
 * No LEA references found — the work item is likely created through
 * a vtable or factory pattern, not direct string reference.
 *
 * The work item is part of the work item queue system:
 *   CWriteFeatureReportWorkItem (0x00aa1880) — sends feature reports
 *   CExitLizardModeWorkItem (0x00aa19e0) — exits lizard mode
 *   CPulseHapticWorkItem (0x00aa18e0) — haptic pulses
 *   CVibrationWorkItem (0x00aa1900) — vibration
 *   CImpulseTriggerWorkItem (0x00aa1920) — impulse triggers
 */

/*
 * === COMPLETE FLOW FOR SET_SETTINGS 0x09 ===
 *
 * 1. CExitLizardModeWorkItem is queued
 * 2. It calls QueueSetting (0x010d5488) with command=0x87, register=0x09, value=0
 * 3. Entry is added to settings array at [r15+0xc0]
 * 4. State machine (0x010d466b) processes the entry
 * 5. SEND: call vtable[0x10] — sends feature report to controller
 * 6. VERIFY: r13 is NULL → SKIPPED
 * 7. If send failed, entry remains in array → retried on next iteration
 * 8. The 3-second retry is the state machine's polling period
 *
 * The verification (vtable[0x130] = get_feature_report) is NOT called
 * because r13 (the verify object) is NULL for SET_SETTINGS.
 */

/*
 * === BINARY REFERENCES ===
 *
 * SET_SETTINGS queue: 0x010d5488
 * State machine: 0x010d466b
 * SEND (vtable[0x10]): 0x010d4e14
 * VERIFY (vtable[0x130]): 0x010d4e83
 * Critical branch (test r13): 0x010d4e6c
 * Pending flag check: 0x010d4da0
 * Comparison path: 0x010d4fd0
 * Timing calc: 0x010d4ff1
 * Settings array: [r15+0xc0]
 * Settings count: [r15+0xd0]
 * Command byte: [r15+0xe0]
 * Report ID: [r15+0x198]
 * Init flag: [r15+0xf0]
 * Pending flag: [r15+0x208]
 */
