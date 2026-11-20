# Motor board deployment — 2026-11-08

Schematic: HARDWARE/Slave Board/SlaveBoard.pdf, sheets 2 and 3.
MCU: STM32F103C8T6, device ID 0x410, flash-size register 64 KiB.
USB serial: 6D81537A5582, Windows COM7. Remote remains on COM6.

Confirmed pin profile: PA8 USB_CTRL through R4=1.5k to USB D+;
PA11 D-, PA12 D+; BTN1/PB13 active low; LED1/PB15.
PB6..PB9 drive opto LEDs through resistors. Low turns optos off; the isolated
TPL7407L input side has pulldowns. Recovery uses the existing active-low BTN1.

## Completed hardware checks

- Read GPIOB ODR before installation: 0x4013 (all DO bits 6..9 low).
- Backed up all 65536 bytes of Flash, then installed factory.hex and verified.
- COM7 answered PING with PONG MOTOR, and STATUS with PWR=0 and zero currents.
- Binary bytes 00..3F echoed exactly (64 bytes).
- Requested BOOTLOADER via application. Loader reported
  APP=08005C00 MAX=39936 VALID=1 TARGET=MOTOR_C8.
- Updated all 38752 application bytes through USB only; CRC32 FFBF51DC matched.
- RUN reset into application; COM7 answered PONG MOTOR again.
- Read the entire Flash via ST-Link HOTPLUG afterward. All factory HEX bytes,
  including boot/application/metadata, matched. Reserved page 0x0800FC00..FFFF
  was identical to pre-install contents.
- Read GPIOB ODR afterward: 0x6013 (all DO bits 6..9 still low).

ST-Link was used for first installation and independent readback only. The
second application programming operation used USB CDC exclusively.

## Backup and update command

Backup: backups/before-install-20261108/full-flash.bin (65536 bytes).
SHA256: e095524c20b1f6f06b2db3321effceaf7df65ace0f8e09d4479c0632866681b3
Independent post-update dump: after-usb-update.bin in the same directory.

```powershell
python tools/usb_update.py dist/application.bin --port COM7 --request
```

Build Application/MDK-ARM/Motor_USB_Application.uvprojx, then regenerate the
package with tools/make_package.py before updating. Do not flash the original
application target at 0x08000000 over an installed bootloader.

## Not tested

No motor ON/OFF commands were sent. Physical output-load behavior, physical
hold-BTN1 recovery, the active-motor refusal condition and interrupted-power
recovery remain untested. RAM and flash have limited headroom on the C8;
future changes must continue to pass the linker bounds.

## USB button command update

Built and updated via COM7; application length 39480, CRC32 60F7C78B.
Tests on board:

- Initial MODE=2 NET=1 NODE=1.
- BTN1/BTN2 CLICK acknowledged; mode unchanged, as in original application.
- BTN CONFIG LONG -> MODE=1 NET=0 NODE=0.
- BTN CONFIRM LONG -> MODE=2 NET=1 NODE=1.
- Invalid BTN3 and invalid action returned ERR BTN without mode change.
- PC helper BTN2 DOUBLE issued two acknowledged CLICK events.
- Final STATUS PWR=0; no motor power commands were sent.

Application partition has 456 bytes of flash remaining after this update;
future features may require size optimization or a revised layout/MCU capacity.
