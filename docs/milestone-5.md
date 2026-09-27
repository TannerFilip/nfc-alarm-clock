# Milestone 5: staged PN7160 physical integration

Firmware 0.5.0 is a development increment for the connected PN7160 board. Its
purpose is to establish a bounded NCI 2.0 transport, passive NFC-A discovery and
the physical-tag path into the existing alarm authorization logic. The
implementation is present, the five-executable host suite passes, and both firmware
profiles build successfully. Every physical result remains pending unless recorded in
`docs/validation.md`.

## Scope and safety gate

This increment initially supports NFC-A identifiers only. It does not implement
all PN7160 technologies, tag data exchange, card emulation or cryptographic card
authentication. An NFCID is an identifier, not proof that a tag cannot be cloned.

Production enabled alarms remain gated until the delivered board passes the full
manual acceptance below. The development profile retains `nfc_sim`, but simulated
and physical events must both call the same bounded tag submission and
authorization path. Neither path may directly silence audio or edit the alarm
journal.

Physical enrollment is separate from ordinary discovery: `tag_enroll` or the
setup page's **Enroll next tapped card** action starts a 60-second window while
idle. It ends on success, timeout or alarm start. A reader fault does not
explicitly cancel it, but cannot extend the original 60-second timeout. An
ordinary tag presentation never silently enrolls a tag. Do not remove the
production gate merely because identifier reads work.

## Transport policy

- GPIO16 drives PN7160 VEN and GPIO15 reads IRQ. The Click board's RST signal is
  ClickID, not PN7160 reset, and is not substituted for VEN.
- Boot, CORE_RESET, CORE_INIT, ISO-DEP discovery mapping, RF discovery,
  `RF_DISCOVER_SELECT` and activation form a monotonic, deadline-bounded state
  machine with delayed retry. The driver drains `MORE` discovery notifications,
  selects a supported final NFC-A candidate, and accepts the UID only from its
  activation notification. No unbounded wait, task spin or recursive retry is allowed.
- The driver waits for IRQ without owning the shared I2C mutex. It reads only when
  IRQ is active and drains pending response/notification data before writing a
  command. Each bus transaction keeps the existing finite transfer deadline.
- NCI's three-byte header and declared payload length are validated before any
  message-specific parse. NCI frames travel directly over I2C with no extra
  length prefix or CRC. A failed write restarts the complete frame through bounded
  recovery.
- Malformed, oversized, unexpected or timed-out frames cause explicit status and
  reader recovery; they do not change alarm state. Reader absence or restart must
  not block the alarm owner, audio task, timekeeping, controls or web UI.
- A valid NFC-A NFCID1 is converted to the existing bounded `TagId` and queued to
  `AlarmService::submit_physical_tag`, which routes normal scans to the same
  `AlarmCore::handle_tag` authorization used by simulation. Unknown tags remain non-dismissing. Repeated reports
  from one continuously held tag are limited by a 1.5-second re-arm interval. If
  the bounded callback queue rejects a submission, the driver retries rather than
  treating that identifier as delivered.

## Before flashing

With power removed, inspect the actual board:

1. Confirm it is the **NFC 7 Click I2C** variant using PN7160A1HN/C100.
2. Confirm VCC SEL is set to **3.3 V** and the I2C option/resistors are populated.
3. Record both ADDR SEL straps. `0/0` selects 7-bit `0x28`; the supported range is
   `0x28`-`0x2B`. Do not enter the eight-bit wire bytes `0x50/0x51` as addresses.
4. Confirm mikroBUS INT reaches GPIO15 (IRQ) and PWM reaches GPIO16 (VEN). Do not
   treat the RST/ClickID connection as PN7160 reset.

For every programming session, **disconnect U3 OUT from U1 5V before connecting
programming USB**. Remove USB before reconnecting U3. Never power U1 from the boost
output and programming USB simultaneously under the prototype wiring guide.

## Build and automated checks

Use the pinned ESP-IDF v6.1 environment from the repository root:

```bash
source /home/tanner/.espressif/tools/activate_idf_v6.1.sh
idf.py --version
idf.py -B build/m1 -D SDKCONFIG=build/m1/sdkconfig build
python3 tools/check_build.py build/m1
idf.py -B build/m1-dev -D SDKCONFIG=build/m1-dev/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.development' build
python3 tools/check_build.py build/m1-dev --development
cmake -S tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
git -c core.whitespace=cr-at-eol diff --check
```

Current host coverage includes NCI header/payload bounds, malformed frames,
exact command bytes, response matching/status, valid NFC-A identifier lengths and
irrelevant technology/interface messages. The IDF driver state machine, timeouts,
recovery and held-tag rate limiting are compile-tested but still require the
physical exercises below. Host tests and successful firmware compilation do not validate electrical selection,
VEN/IRQ timing, antenna behavior or an actual RF read.

## Physical acceptance

Flash the development profile first, following the power rule above. Capture a
complete serial log rather than reporting only the final screen.

1. Confirm the `milestone 5 / 0.5.0` development banner, expected flash/PSRAM and
   development warning. Run `i2c_scan` for the non-NFC peripherals. It deliberately
   skips 0x28-0x2B because a probe is a write that may violate PN7160 IRQ ordering;
   compare the address identified by a valid NCI response with the recorded straps.
2. Confirm logs/status separately show VEN boot, NCI CORE_RESET response,
   CORE_INIT response and active NFC-A discovery. Record the selected 7-bit
   address and any status/error bytes. IRQ must be inactive while idle and become
   readable when the controller has a response/notification.
3. With no tag, exercise the clock display, encoder/buttons, `clock_status`, web
   setup and an idle `audio_test 5`. Reader polling must not delay or break them.
4. Present one NFC-A tag briefly. Confirm a bounded identifier event reaches the
   normal tag path. Hold it in the field for at least ten seconds and confirm the
   de-duplication/re-arm interval prevents a tight queue/log flood. Remove it,
   wait for discovery to re-arm, and present it again; a new event should then be
   accepted.
5. While idle, run `tag_enroll` or select **Enroll next tapped card** on the setup
   page. Present one tag within 60 seconds and confirm durable settings acceptance. Repeat
   without enrollment mode and confirm the stored tag set does not change. Let a
   second window expire and confirm a late scan is not enrolled.
6. Trigger a development alarm. Present a different, unknown tag and verify audio
   continues and `alarm_status` remains ringing. Present the enrolled physical
   tag; verify durable dismissal occurs before audio stops and active count becomes
   zero. Buttons, encoder, setup/configuration and reader recovery must not bypass
   this path.
7. Trigger again and remove power only after the active occurrence is durable.
   Reboot and verify ringing resumes independently of reader initialization.
   Present an unknown tag, then the enrolled tag. Reboot once more and verify the
   dismissed occurrence does not return.
8. Power down, disconnect the PN7160, then boot. Confirm an explicit unavailable/
   retrying status while the clock, UI and alarm audio remain functional. Power
   down before reconnecting it, reboot, and confirm reset/init/discovery recover.
   Do not hot-plug this energized breadboard to simulate a fault.
9. Repeat the enabled-alarm, unknown-tag and enrolled-tag checks with the production
   profile only after all preceding evidence passes and the production gate is
   deliberately removed in a reviewed change. Confirm production still contains
   no `nfc_sim` or development-only mutation commands.

## Acceptance status

| Check | Result |
| --- | --- |
| Portable NCI parser/session tests | PASS; host CTest suite 5/5 |
| Production firmware build/checker | PASS; app `0xf5cf0`, SHA-256 `9b528526381ddd293d13c8451557de12d28421455f52aa41706dc1ccf77c1a20` |
| Development firmware build/checker | PASS; app `0xf6080`, SHA-256 `e2a972449dceaf8445eca080e9aa7c5d2078effeb92fa8704acd32f20806f0b9` |
| I2C variant, 3.3 V selection and straps inspected | PENDING |
| VEN/IRQ and NCI reset/init observed | PARTIAL; user log reaches NCI ready at `0x28`; electrical timing not observed |
| NFC-A read and held-tag behavior observed | PARTIAL; corrected read works per user, held-tag behavior PENDING |
| Explicit physical enrollment observed | PASS, user-reported |
| Unknown/enrolled tag alarm behavior observed | PENDING |
| Active reboot recovery with physical dismissal observed | PENDING |
| Reader disconnect/reconnect recovery observed | PENDING |
| Production enabled-alarm gate removal | BLOCKED on physical acceptance |

The user reports successful NFC-A reading and explicit enrollment on the corrected
development image. Update the remaining rows only with reproducible evidence,
distinguishing user-reported from agent-observed results.
