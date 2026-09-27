# Future features

Requested by the user after milestone 1 hardware confirmation. These are planning
items, not implemented capabilities or instructions to change hardware now.

## Battery charge limit

Aim to keep the battery around 80% during normal plugged-in use, while retaining
enough energy to carry the clock to the NFC tag. Hardware changes may be needed.
Before implementation, review charger control options, power-path behavior and
state-of-charge measurement against manufacturer documentation. Do not equate
an uncalibrated voltage threshold with an accurate 80% charge level. Define the
charge/resume policy and failure behavior before selecting hardware or firmware.

## Occasional Wi-Fi time synchronization

Extend the planned optional network synchronization into periodic time updates.
Keep RTC-backed operation independent of network availability. Choose the interval,
connection/retry budget and clock-correction policy during timekeeping integration;
expose the last successful sync and time validity. Corrections must obey the
documented missed-alarm and duplicate-occurrence policies.

## Deliberate backup dismissal

Provide an alternative if NFC dismissal is unavailable. Candidate approaches are
a recessed/pinhole physical button or an authenticated webpage requiring a
physical action, potentially checked through the phone's sensors. A simple web
"stop" button does not satisfy the user's intent.

The mechanism and required action are undecided. Before implementation, define
what physical effort/presence is intended, how activation works while ringing,
and how accidental use is prevented. For a phone-based option, investigate actual
browser sensor support, permissions, secure-context requirements and the limits
of treating client-reported sensor events as evidence. Do not promise sensor
availability or tamper resistance without verification.

Once selected, document this as an explicit exception to NFC-only dismissal in
the architecture and agent instructions. Route it through the alarm state machine
and durable dismissal journal, with tests for authorized use and bypass attempts.
Until then, preserve the current NFC-only production policy and development-only
simulation gate. Ordinary controls/configuration must never silently dismiss.

## Physical tag enrollment UX

Milestone 5 introduces a narrow 60-second enrollment window through `tag_enroll`
and the temporary setup page. The final on-device interaction and review remain a
design task. Enrollment must stay explicit, conspicuous and idle-only; normal
background scans must never add a tag. The flow must report the identifier stored,
reject duplicate/invalid identifiers, persist settings before confirming success,
and cancel on timeout or alarm start. A reader fault must not extend the original
60-second timeout. After the basic reader
transport passes device validation, decide whether to add a deliberate physical
control gesture and how the OLED should expose the active window.
