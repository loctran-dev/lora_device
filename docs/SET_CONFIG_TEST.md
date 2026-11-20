# SET_CONFIG hardware check — 2026-11-08

Remote COM6, slave COM7 (NET=1, NODE=1). No power-control command sent.
Historical RAM-only test before persistence was implemented. Current behavior
and reset/Flash tests are documented in [PERSISTENCE.md](PERSISTENCE.md).
Used remote USB button injection to edit/save through the actual UI; the
configuration itself travelled over LoRa, not USB.

Read slave RAM through ST-Link SWD HOTPLUG (no halt/reset/write). Matched symbols
from rebuilt Motor_USB_Application: sysInfo=0x200003A4; DWARF member offsets
820/822, hence threshold/delay at 0x200006D8/0x200006DA.

| UI save | Slave threshold (0.01 A) | Slave delay (s) |
| --- | --- | --- |
| 1.00 A, 1 s | 100 | 1 |
| 1.01 A, 1 s | 101 | 1 |
| Restore UI values: 1.00 A, 0 s | 100 | 0 |

Before testing, slave RAM was 0/0 (boot defaults), while remote had 1.00 A/0 s.
After testing the remote's original settings were restored and synchronized to
the slave. Final USB STATUS: PWR=0, all three currents=0.

Result: valid SET_CONFIG reception and RAM application confirmed on hardware.
ACK reception at the remote was not independently instrumented. Malformed
requests, packet loss/retry and persistence were not tested. This does not test
or establish motor protection: the received fields are not yet used by the
protection loop and are not saved to Flash.
