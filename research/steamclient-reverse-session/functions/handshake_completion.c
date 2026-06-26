/*
 * Handshake Completion Analysis — Does It Complete Despite Retries?
 *
 * Binary: ~/.steam/debian-installation/linux64/steamclient.so
 * Status: DETERMINED
 */

/*
 * === EXECUTIVE SUMMARY ===
 *
 * YES, the handshake completes despite SET_SETTINGS retries.
 *
 * The SET_SETTINGS retry loop runs in the state machine (0x010d466b)
 * which is a SEPARATE path from the controller registration flow.
 * The registration flow completes via:
 *   1. EYldWaitForControllerDetails (0x01071c70)
 *   2. QueueFetchingControllerDetails (0x01092820)
 *   3. BYieldingCompleteSteamControllerRegistration
 *
 * These functions check the ControllerDetails ready_flag at offset 0x3c,
 * NOT the SET_SETTINGS state machine. The SET_SETTINGS retries are
 * "noise" that runs in parallel but doesn't block registration.
 */

/*
 * === REGISTRATION FLOW ===
 *
 * 1. EYldWaitForControllerDetails (0x01071c70)
 *    - Waits for controller details to be populated
 *    - Uses 2-second timeout (0x1e8480 microseconds)
 *    - Checks ready_flag at offset 0x3c
 *    - If ready_flag == 1, proceeds to registration
 *
 * 2. QueueFetchingControllerDetails (0x01092820)
 *    - Copies 0x54-byte ControllerDetails to controller slot
 *    - Checks ready_flag at [rsi+0x3c]
 *    - If ready_flag != 0, jumps to completion path (0x10929f0)
 *    - If ready_flag == 0, queues settings and returns
 *
 * 3. BYieldingCompleteSteamControllerRegistration
 *    - Finalizes registration with Steam servers
 *    - Error strings found:
 *      "Error committing registration"
 *      "couldn't get controller info"
 *      "timed out"
 *      "controller disconnected"
 */

/*
 * === READY FLAG CHECKS ===
 *
 * The caller at 0x010b2ca0 checks TWO flags:
 *
 *   0x010b2d4c: movzx eax, byte [rdi+0x28]  ; primary readiness
 *   0x010b2d50: test al, al
 *   0x010b2d52: je 0x10b2d61                 ; skip if not ready
 *
 *   0x010b2d54: cmp byte [rdi+0x80], 0       ; secondary completion
 *   0x010b2d5b: jne 0x10b2e98                ; if set, success path
 *
 * In QueueFetchingControllerDetails:
 *   0x010928ad: cmp byte [rsi+0x3c], 0       ; ready_flag
 *   0x010928b1: jne 0x10929f0                ; if != 0, completion path
 *
 * These flags are set by the controller firmware responding to
 * GET_ATTRIBUTES/0xf2/GET_SERIAL — NOT by SET_SETTINGS.
 */

/*
 * === SET_SETTINGS RETRY IS NOISE ===
 *
 * The SET_SETTINGS retry loop runs in the state machine at 0x010d466b.
 * This is a SEPARATE execution context from the registration flow:
 *
 * - Registration: EYldWaitForControllerDetails → QueueFetchingControllerDetails
 * - SET_SETTINGS: State machine → vtable[0x10] → retry
 *
 * The state machine processes settings entries at [r15+0xc0].
 * The registration flow checks ControllerDetails at [rsi+0x3c].
 * These are DIFFERENT data structures accessed by DIFFERENT functions.
 *
 * The SET_SETTINGS retries do NOT prevent registration from completing.
 * They are background noise that continues even after registration succeeds.
 */

/*
 * === THREAD MODEL ===
 *
 * The state machine at 0x010d466b is likely called from a worker thread
 * (CHIDIOThread or similar). The registration flow runs on the main
 * thread or a yield thread. They execute independently.
 *
 * Evidence:
 * - "CSteamController::CHIDIOThread" at 0x00d6fbc2
 * - "CSteamController::CHIDIOThread::CWorkItemThread" at 0x00d73b6a
 * - Work items include: SetUserLedColor, ResetLedColor, TurnOffController
 *
 * The SET_SETTINGS work items are queued to CHIDIOThread, which processes
 * them asynchronously. The registration flow waits for ControllerDetails
 * (which come from GET_ATTRIBUTES/0xf2), not from SET_SETTINGS.
 */

/*
 * === CONCLUSION ===
 *
 * The SET_SETTINGS retry loop does NOT block controller registration.
 * The registration completes when:
 *   1. Controller responds to GET_ATTRIBUTES/0xf2 with capability data
 *   2. ControllerDetails ready_flag at offset 0x3c is set to 1
 *   3. EYldWaitForControllerDetails unblocks
 *   4. QueueFetchingControllerDetails copies details and completes
 *
 * The SET_SETTINGS retries are a SEPARATE concern — they affect
 * lizard mode state, not registration. If SET_SETTINGS keeps failing,
 * lizard mode stays ON, but the controller still registers and works.
 *
 * If the user's issue is that registration is blocked, the problem
 * is NOT SET_SETTINGS retries. It's likely:
 *   - GET_ATTRIBUTES/0xf2 not completing (different issue)
 *   - ready_flag not being set (firmware issue)
 *   - Timeout in EYldWaitForControllerDetails (2-second limit)
 */

/*
 * === BINARY REFERENCES ===
 *
 * EYldWaitForControllerDetails: 0x01071c70
 * QueueFetchingControllerDetails: 0x01092820
 * Caller of QueueFetchingControllerDetails: 0x010b2ca0
 * State machine: 0x010d466b
 * SET_SETTINGS queue: 0x010d5488
 * Ready flag check: 0x010928ad (cmp byte [rsi+0x3c], 0)
 * Registration completion: 0x010929f0
 * CHIDIOThread string: 0x00d6fbc2
 * BYieldingCompleteSteamControllerRegistration strings: 0x00cc65d8, 0x00d17710, 0x00d526e8
 */
