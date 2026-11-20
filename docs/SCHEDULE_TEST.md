# Timer retest - 2026-11-15

## Retest after user's edit

The requested comparison is now payloadRequest.scheduleTime == 0. Built and
flashed app only: 39796 bytes, CRC32 03E2532F verified, no errors/warnings.
36 normal tests pass. Explicit schedule diagnostic now passes 9/10: initial
timer registration is fixed; clearing scheduleTime at expiration is still FAIL.
No application main.c change or bootloader programming was performed.

With the user's fresh no-load ON approval, physical 00h01 retest PASS. Remote UI
countdown was seen at 53 and 21 seconds remaining, and slave became OFF between
the 57.2/62.4-second polls. Cleanup OFF was sent and PWR=2 confirmed. See the
remote report for screenshots and preserved configuration. Remote app unchanged.

## Historical result before user's edit

Current firmware fails the first-timer-after-ON case. Full bench results and
remote packet checks: [remote report](../../Lora_remote/docs/SCHEDULE_TEST.md).

No firmware was edited, built or flashed during this test. User authorized a
no-load ON test for one minute. Native test remained ON at 65.2 seconds; remote
OFF cleanup was confirmed by slave USB STATUS PWR=2.

Run `python tools/check_schedule.py` for explicit built-ARM diagnostics:
10 checks, 8 pass, 2 intentionally failing assertions for unresolved defects.

- commandHandlers.c:266 checks old sysInfo.scheduleTime == 0, discards the first
  future deadline after ON and still returns ACK result=1.
- main.c:776 stops at a preexisting nonzero deadline but leaves scheduleTime
  nonzero afterward, so expired timer data remains in STATUS.

Both failed cases are tested without physical outputs, using Unicorn and mocked
RTC/HAL/queues. Existing replacement, cancellation and validation checks pass.
Do not rerun the physical ON test with a real load without explicit approval.
