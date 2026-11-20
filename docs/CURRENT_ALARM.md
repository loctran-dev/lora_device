# Configured low-current alarm

The existing low-current warning now uses alarmThresholdCentiAmp / 100 A and
alarmDelaySeconds instead of fixed 0.5 A / 30 seconds. This deliberately retains
the original comparison (any phase current LESS than threshold), not overcurrent.
The remote's old overcurrent wording must not be taken as overcurrent protection.

Behavior in SYSSTATE_RUNNING:

- MOTOR_POWER_ON and any phase below the nonzero threshold: assert the alarm
  once the configured delay expires.
- Current recovers, power is not ON, or threshold is zero: clear the flag and
  reset the debounce timestamp.
- Threshold zero disables this warning; delay zero means no intentional delay.
- Delay seconds are converted directly to ticks, avoiding intermediate
  millisecond multiplication overflow. Unsigned subtraction handles tick wrap.
- Existing alarm packet format/code handling and 15-second retransmission
  cadence are unchanged. No automatic motor shutdown was added.

Motor_CurrentAlarmDue(now) is isolated so tests execute the exact built logic.
Repeated packet construction was shared in Motor_SendCurrentAlarm to fit the
existing partition. The journal blank-slot check was expressed as bitwise AND
of all words (equivalent to checking every word is erased); persistence tests
including torn slots and page-full behavior still pass.

## Deployment and verification

Slave app: 39932 bytes, CRC32 C33D353D, flashed over COM7. Bootloader/settings
layout unchanged. Only 4 bytes remain below the 39936-byte application limit;
future additions will need code-size work or a deliberate layout redesign.
Remote firmware was not changed for this feature.

Build passed. 27 slave tests passed: 9 alarm logic tests, 10 persistence/handler
tests and 8 USB package tests. Alarm tests run actual Thumb code in Unicorn:
each phase, configured delay boundary, equality/above threshold, recovery reset,
disabled threshold, motor OFF, initial-event deduplication, 9999-second delay,
and tick wraparound. They do not actuate physical outputs.

On hardware, sent 1.00 A / 2 s from remote; slave MODE reported I=100 D=2.
After reset it still reported I=100 D=2. Restored the pre-test 0 / 0 settings
through the remote and confirmed MODE I=0 D=0 and STATUS PWR=0, all currents=0.
Real motor/current-triggered alarm testing was intentionally not performed.
The remote USB cable was reconnected during testing after COM6 disappeared.
Evidence: Lora_remote/dist/current-alarm-config-1A-2s.bmp.

Run from the slave project root:

```powershell
python tools/test_current_alarm.py
python tools/test_persistence.py
python tools/test_update.py
```
