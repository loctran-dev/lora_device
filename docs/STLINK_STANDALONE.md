# Slave standalone ST-Link app (temporary no-bootloader mode)

The user approved removing the bootloader to free Flash for history.
Build Application/MDK-ARM/Motor_USB_Application.uvprojx, target Motor_USB_Application.
The legacy filename is retained, but this target now links at 08000000 and runs
WITHOUT a bootloader. Main VTOR is 08000000. The alias LoraDevice_SingleMotor
project also uses the protected application.sct layout.

Run python tools/make_package.py. Current outputs:

- dist/application.hex: ST-Link image, program starting at 08000000.
- dist/application.bin: binary with origin 08000000.
- dist/factory.hex: same standalone image; NO bootloader or boot metadata.
- dist/legacy-usb-factory.hex: archived old USB-bootloader factory image,
  for deliberate recovery ONLY. Never use it for a normal history app update.

The old tools/usb_update.py remains a legacy MUP1 recovery client; its vector
checks reject the new standalone binary. USB CDC diagnostics continue working.
The app refuses BOOTLOADER with ERR STLINK_ONLY instead of resetting into nothing.
Bootloader/ sources and build artifacts remain available, but are NOT linked or
required by the current standalone package.

Before migration, the entire physical 64 KiB slave Flash was backed up to
backups/history-stlink-20261118/slave-original-64k.bin.
SHA256: BBBA479706DA7CF2DEF8542AF694E90CFBB579AAFDC794070220B08D520A501C.
This includes the original bootloader, app and configuration. config-after.bin
was read after migration and compared byte-for-byte with the backup's FC00..FFFF:
identical. The connected ST-Link serial was 3E48130012144D43574D4E00, device
ID 410 / STM32F103C8 / 64 KiB. Remote was NOT attached to this ST-Link.

Programming performed with motor confirmed OFF, using sector-only erase:

```powershell
& 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe' -c port=SWD mode=UR freq=1000 -w dist/application.hex -v -rst
```

No mass erase. Cube erased app sectors 0..42 only. History DC00..FBFF and config
FC00..FFFF are excluded from the HEX image and must remain excluded on later
updates. See HISTORY.md for capacities and stream-reset limitations.

Restoring the full old backup is a separate deliberate operation: it replaces
the app/bootloader/settings with their pre-migration versions and destroys newer
slave history. Do not automatically restore it as part of normal testing.
