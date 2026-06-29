# SC2 BLE Spoof — Autonomous Haptics Investigation

## Boot Sequence

1. Read `AGENTS.md` — full project context, architecture, how to run
2. Read `docs/investigation-plan.md` — methodology rules, confidence scale, hypothesis lifecycle
3. Read `HANDOFF.md` — current status
4. Read `docs/findings-backlog.md` — what's been tried, what hasn't
5. Read `research/steamclient-reverse-session/findings.md` — RE context
6. Then proceed to investigation below

## Environment

```
Deck IP:       172.16.16.120
Deck SSH:      sshpass -p 'asdf' ssh -o StrictHostKeyChecking=no deck@172.16.16.120
Deck sudo:     echo 'asdf' | sudo -S <cmd>
Host sudo:     printf 'qwerasdf\n' | sudo -S <cmd>
BLE address:   C2:12:34:56:78:9A
Host BT MAC:   9C:B6:D0:8F:97:68
Source:        /home/philip/spoofdeck-modified/src/
Remote dest:   /tmp/sc2-spoof/src/
```

Source `pii.env` for credentials before scripts.

## The Problem

Everything works except haptics. On a **clean connection** (stale state cleared):

- Host sends only Write Requests (0x12) to handle 0x0024 (SET_SETTINGS 0x87) every 3 seconds
- **Zero Write Commands (0x52)** — host never sends haptic output reports
- **Zero SET_REPORT attempts** — hog-ll never tries to configure output reports
- Steam schedules haptics (`CPulseHapticWorkItem`) but write completes in 0.0ms (rejected at kernel level)

The SET_SETTINGS notification hypothesis was **TESTED AND FAILED** (caused ghost inputs). It is NOT the blocker.

The root cause is: **hog-ll never attempts SET_REPORT on the clean connection.** Without SET_REPORT, the output report path is never established and haptic writes from Steam are rejected.

## Investigation: Why Doesn't hog-ll Attempt SET_REPORT?

This is the core question. The answer is in BlueZ's hog-lib.c source code.

### Phase 1: Get BlueZ Source

BlueZ 5.86 is on the Deck. Get the source:

```bash
sshpass -p 'asdf' ssh -o StrictHostKeyChecking=no deck@172.16.16.120 \
  "echo 'asdf' | sudo -S apt-get source bluez 2>/dev/null || \
   echo 'asdf' | sudo -S apt-get download bluez 2>/dev/null && \
   dpkg-deb -x bluez*.deb /tmp/bluez-src/"
```

If that fails, the source is at `/usr/share/doc/bluez/` or download from kernel.org. The key file is `profiles/input/hog-lib.c`.

### Phase 2: Understand SET_REPORT Flow (use explore subagent)

Spawn an explore subagent to read `hog-lib.c` and answer:

1. **When does hog-ll call SET_REPORT?** — What triggers it? Is it during initialization or on-demand?
2. **What conditions must be true before SET_REPORT is attempted?** — Does it need specific Report Map entries, CCCD subscriptions, or other prerequisites?
3. **What opcode does hog-ll use for SET_REPORT?** — ATT Write Request (0x12) or ATT Write Command (0x52)?
4. **What handle does it write to?** — The CHR_REPORT handle for the output report (should be 0x0019 for Report ID 0x80)
5. **What data does it send?** — Report ID + report data? Or something else?
6. **Does hog-ll skip SET_REPORT for BLE vs USB?** — Is there a code path difference?
7. **What happens after SET_REPORT succeeds?** — Does it then enable output via uhid? Does it change how SDL_hid_write works?
8. **Is there a "boot protocol" vs "report protocol" distinction?** — Does hog-ll need to be in a specific mode?
9. **What does `set_report_cb()` do on success vs failure?** — Does it retry? Does it disable output?
10. **Is there a HID Control Point interaction?** — Does SET_REPORT require SET_PROTOCOL first?

Also read `src/shared/uhid.c` to understand:
- How `bt_uhid_set_report()` works
- What the uhid device configuration looks like
- Whether output reports are supported in the uhid setup

### Phase 3: Compare with Real SC2 (if possible)

Check if there's a real SC2 BLE capture available:
```bash
ls -la /home/philip/spoofdeck-modified/scratch/
find /home/philip/ -name "*.btsnoop" -o -name "*.log" 2>/dev/null | head -10
```

If a real SC2 capture exists, compare the initialization sequence with our fake device. Look for:
- Does the real SC2 get SET_REPORT attempts?
- What's different about the real device's HID descriptor?
- What handles does the real SC2 use?

### Phase 4: Test Hypotheses

Based on Phase 2 findings, form hypotheses and test them one at a time:

**Possible hypotheses (ranked by likelihood):**

1. **hog-ll doesn't SET_REPORT because output reports aren't enabled in uhid** — The uhid device setup might not advertise output report capability. Check `bt_uhid_set_report_size()` or similar.

2. **hog-ll requires SET_PROTOCOL before SET_REPORT** — The HID Control Point (handle 0x0010) might need a specific protocol mode set first. Check if our server handles Control Point writes correctly.

3. **hog-ll needs the Report Map to be read before attempting SET_REPORT** — If the Report Map read fails or returns wrong data, hog-ll might skip output report configuration.

4. **Steam/SDL doesn't request haptics because capabilities bitmask is wrong** — The GET_ATTRIBUTES response returns `0x4169bfff`. Check if bit 37 (haptics) is set. If not, Steam might skip haptic initialization.

5. **hog-ll only SET_REPORTs after receiving specific SET_SETTINGS** — Steam might need to write certain settings registers before hog-ll enables output. Check if our SET_SETTINGS handler stores values correctly.

**Testing approach:**
- For each hypothesis: predict what you'd observe IF true, make ONE change, test, evaluate
- Deploy changes via: `sshpass -p 'asdf' scp src/*.py deck@172.16.16.120:/tmp/sc2-spoof/src/`
- Restart service: `sshpass -p 'asdf' ssh deck@172.16.16.120 "echo asdf | sudo -S systemctl restart sc2-hogp"`
- Connect from host: `printf 'qwerasdf\n' | sudo -S bluetoothctl connect C2:12:34:56:78:9A`
- Check btmon: `printf 'qwerasdf\n' | sudo -S timeout 10 btmon -t 2>&1 | grep -E "Write|0x52|Error|SET_REPORT"`
- Check Deck logs: `sshpass -p 'asdf' ssh deck@172.16.16.120 "echo asdf | sudo -S journalctl -u sc2-hogp --since '2 min ago' --no-pager | grep -i 'haptic\|write.*0x0019\|SET_REPORT'"`

### Phase 5: If BlueZ Source Unavailable

If you can't get the BlueZ source, investigate from the other end:

1. **Check HID Control Point handling** — Our `_on_feature_report_write` handler processes SC2 commands on FR 0x00/0x01, but does it handle Control Point writes (handle 0x0010)? If hog-ll writes to the Control Point and we ignore it, SET_REPORT might never be triggered.

2. **Check Report Map parsing** — Our Report Map declares 234 bytes. Verify each section is correct by parsing it and comparing with the real SC2 spec.

3. **Check if SET_PROTOCOL is needed** — The HID Control Point characteristic (handle 0x0010) accepts SET_PROTOCOL and SET_IDLE commands. If hog-ll sends SET_PROTOCOL and we don't respond, it might skip output.

4. **Try forcing SET_REPORT from our side** — As a diagnostic, have our server initiate a fake SET_REPORT by sending a notification that includes output report data. If this triggers hog-ll to establish the output path, we know the issue is initialization order.

## Important Rules

1. **Do NOT pair with pairing code** — Use `bluetoothctl connect` only. Pairing requires clicking "yes" on KDE dialog which needs human intervention. If pairing is needed, clear bond data and reconnect.

2. **One change at a time** — Never stack fixes. If Fix A doesn't work, revert it, then try Fix B.

3. **Evidence before conclusion** — Every finding must cite specific evidence. Tag with confidence level (Confirmed/Plausible/Speculative).

4. **Spawn subagents for research** — Don't read 500+ lines of BlueZ source in the main thread. Use explore subagents.

5. **Commit progress** — After each meaningful finding, commit: `git add -A && git commit -m "finding: <description>" && git push`

6. **Stale state is the #1 cause of mysterious failures** — Every test cycle: clear bond data, restart BlueZ, reconnect.

7. **The answer is likely in hog-lib.c** — The `hog_set_report()` function, the `hog_halt()` function, and the initialization sequence are the key areas.

## Deliverables by Morning

1. **Root cause** — Why doesn't hog-ll attempt SET_REPORT on the clean connection?
2. **Fix** — If fixable, implement and deploy
3. **If not fixable** — Document exactly why (BlueZ limitation, hardware limitation, etc.) and what workaround might exist
4. **Updated docs** — All findings documented with evidence and confidence levels
