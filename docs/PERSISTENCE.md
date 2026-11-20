# Slave persistent settings

Application journal: last reserved Flash page 0x0800FC00..0x0800FFFF (1 KiB).
App remains 0x08005C00..0x0800F7FF, metadata 0x0800F800..0x0800FBFF.
No bootloader or linker layout changes were required.

Each 20-byte record contains five little-endian uint32 words:

1. Threshold in 0.01 A (low 16 bits), delay in seconds (high 16 bits).
2. LoRa frequency in MHz.
3. Net ID, node ID, SF in successive low bytes.
4. Existing calculateCRC result over the first 12 bytes.
5. Commit/version magic 0x31474643 (written last).

Load scans for the last committed, CRC-valid, range-valid record. Defaults are
retained if there is none. Identical settings do not write again. Torn/invalid
slots are skipped. There are 51 records per page; when no blank slot remains,
the page is erased and a fresh record written. Power loss during that erase or
first replacement write can lose settings: this is not a two-page atomic store.
Configuration saves are rejected while sysInfo.powerState is MOTOR_POWER_ON.

SET_CONFIG saves threshold/delay before ACK=1; failure returns ACK=0 and restores
the previous RAM values. CONFIG_CONNECTION similarly saves net/node/frequency/SF
before success ACK, retaining the existing physical configuration/confirm flow.
Startup loads settings before radio initialization so persisted radio parameters
are actually applied. Motor power and schedules are not persisted or replayed.

USB MODE now includes I (0.01 A), D (seconds), F (MHz), SF for readback. Both
update/package tools exclude the settings page, so normal USB app updates retain it.

To fit the unchanged app partition, duplicate response and payload-parse code was
shared, and an unnecessary full-packet copy removed. LoRa AutoLDO uses equivalent
integer arithmetic; all 70 combinations of SF6..12 and existing bandwidth entries
were checked against the former expression. Motor current math was not changed.

Persistence-stage image: 39636 bytes, CRC 5BDEFC73 (300 bytes below app limit).
The subsequent configured-current alarm build is documented in CURRENT_ALARM.md.
RAM: RW=592, ZI=19216, below 20 KiB. Further features must respect both limits.
Build passed, existing missing-newline warning in button.h remains.

Tests: tools/test_persistence.py executes the actual built ARM journal/connection
handler with mocked Flash/queue HAL calls (10 tests). tools/test_update.py passes
8 host tests. On hardware, alarm 0.01 A / 1 s survived reset; after restoration,
0 / 0 also survived reset. Network-changing pairing and Flash-failure recovery
were emulated rather than disrupting live pairings.

The subsequent current-alarm change consumes these fields for the existing
LOW-current warning. See CURRENT_ALARM.md; overcurrent shutdown is not implemented.
See [remote test overview](../../Lora_remote/docs/PERSISTENCE.md) for full results.
