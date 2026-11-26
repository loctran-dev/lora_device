# Autonomous status and remote link supervision

Implemented 2026-11-09. See
[remote LINK_SLEEP.md](../../Lora_remote/docs/LINK_SLEEP.md) for settings,
supervision, sleep behavior, persistence migration and current test limits.

Slave additions:

- Auto status period: 0..9999 seconds, default 60, 0 disables.
- Measures from the last SUCCESSFUL STATUS TX, including solicited replies.
- A 1-second timer only queues reports; the existing TX task sends them.
- Pending flag is set BEFORE publishing to the queue, because the TX task can
  preempt the timer and finish before osMessageQueuePut returns.
- Reports retain the configured node ID, use RESPONSE and unsolicited TXID 0.
- Configuration mode suppresses auto reports. USB MODE reports A=period.
- SET_CONFIG opcode 08 accepts packed little-endian payloads of 5 or 7 bytes.
  The extra u16 at offset 5 is the auto period. Legacy requests preserve it.
- Settings remain in the existing 20-byte journal at 0x0800FC00; bootloader and
  application partition boundaries are unchanged. Never erase this page to update.

The journal's former checksum word now holds tag A5 in bits 31..24, period in
bits 23..8 and negative byte-sum in bits 7..0. The first 16 bytes sum to zero.
Legacy records with only an 8-bit checksum load with period 60. Commit magic
is written last, unchanged. Rejected or failed config commits restore RAM values.

Removed libc calendar conversion dependency to fit the C8 application partition.
UTC conversion supports 2000..2099 and initializes year/month/day/weekday.
Current image is 39796 / 39936 bytes (140 bytes remaining), CRC32 F8A7A02A.
Further features will need space optimization or an explicitly approved layout
change. The bootloader binary was NOT reflashed.

36 host/ARM-code tests pass, including pending-TX preemption and 325 calendar
roundtrips. USB app update verified CRC. Hardware SET_CONFIG and repeated status
were checked with PWR=0; no ON command or motor output test was performed.

## Status for every motor (2026-11-26)

CMD_GET_STATUS with moduleIndex 0xFF (LORA_MODULE_ALL) is answered with a
6-byte header (0xFF, local time, count) and one 13-byte PayloadStatusRecord_t
per motor; SingleMotor sends count = 1 (module 0). moduleIndex 0 still gets
the 17-byte single layout; other values are ignored. Autonomous reports now
use the 0xFF layout. The remote side and compatibility notes are in
Lora_remote/docs/STATUS_ALL.md. Tests: test_status_clock, test_auto_status.
Bench: application 44380 bytes, CRC32 93385914, ST-Link sectors 0..43;
configuration (NET=1 NODE=1) kept.
