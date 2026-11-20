# Motor controller USB/bootloader variant (INSTALLED)

Documentation is stored in `docs/`. All paths and commands below are relative
to the project root `FIRMWARE/Lora_device`, not this documentation directory.

This is an isolated snapshot of LoraDevice_SingleMotor. The original source
and the remote board's installed firmware have not been changed.
SlaveBoard.pdf confirms the C8 MCU, PA8 USB control, PB13 active-low recovery
button and active-high opto inputs. Installed and verified on the motor board;
see DEPLOYMENT.md. Its USB CDC port is COM7; COM6 belongs to the remote.

## Build targets

- Application/MDK-ARM/Motor_USB_Application.uvprojx
- Bootloader/MDK-ARM/Motor_USB_Bootloader.uvprojx

Both use ARM Compiler 5 and the configured STM32F103C8T6 limits: 64 KiB Flash,
20 KiB RAM. Do not assume undocumented 128 KiB Flash on a C8 device.

| Region | Start | End inclusive |
| --- | --- | --- |
| Bootloader, 23 KiB | 0x08000000 | 0x08005BFF |
| Application, 39 KiB | 0x08005C00 | 0x0800F7FF |
| Commit metadata, 1 KiB | 0x0800F800 | 0x0800FBFF |
| Reserved settings, 1 KiB | 0x0800FC00 | 0x0800FFFF |

The application snapshot has no flash-backed settings implementation; its
existing defaults/RTC behavior are retained. The reserved settings page is
neither written nor erased by the updater. Flash is tight: always check the
linker bounds when adding features. The custom scatter file enforces them.

## Application USB CDC

Existing CubeMX middleware is reused. USB_CTRL is PA8 and is raised before
USB init. Commands are ASCII LF, sent one at a time in one write (up to 64
bytes); wait for the reply. These commands are distinct from the remote UI API.

- PING -> PONG MOTOR
- MODE -> MODE=... NET=... NODE=... (0=idle, 1=configuration, 2=running)
- BTN BTN1 CLICK or BTN BTN1 LONG: inject config-button event.
- BTN BTN2 CLICK or BTN BTN2 LONG: inject confirm-button event.
  CONFIG/CONFIRM are aliases for BTN1/BTN2. BTN commands accept LF or CRLF,
  one complete line per write, maximum 63 bytes. Invalid lines return ERR BTN.
  LONG invokes the event immediately; it does not wait one second or drive GPIO.
  CLICK is acknowledged but has no application action, matching current physical
  short-click behavior. BTN1 LONG enters LoRa configuration (NET/NODE=0);
  BTN2 LONG restores running mode and configured NET/NODE.
- STATUS -> STATUS PWR=... A_MA=... B_MA=... C_MA=... (currents in milliamperes)
- BOOTLOADER -> OK BOOTLOADER, then reset into loader, only if motor/output state
  is inactive. There is a second check immediately before reset.
- Other packets are echoed; there are no USB motor ON/OFF commands.

PC examples (DOUBLE means two CLICK events, not a separate firmware gesture):

```powershell
python tools/motor_button.py BTN1 LONG --port COM7
python tools/motor_button.py CONFIRM LONG --port COM7
python tools/motor_button.py BTN2 DOUBLE --port COM7
```

Replies are queued to systemTask, never transmitted with blocking waits inside
the USB IRQ. OUT is held until IN completion so the buffer is not overwritten.
The original task's three-second startup indication can delay initial replies.
USB RX/TX buffers were reduced from 1024 to 64 bytes to fix the baseline RAM
overflow without reducing the existing 10 KiB FreeRTOS heap or task stacks.

## Bootloader board profile (confirmed by schematic)

- MCU: STM32F103C8T6; medium-density 1 KiB erase pages.
- USB attach: PA8 high; 8 MHz HSE, PLL x9, 48 MHz USB.
- Recovery: BTN1/PB13, active low, held through reset; input uses pull-up.
- Boot heartbeat: LED1/PB15.
- DO1..DO4/PB6..PB9 driven low in loader, matching application reset levels.
  The schematic's opto LEDs are off at these levels; TPL7407L inputs are pulled
  down. Observed PB6..PB9 low before installation and after the USB update.
- Loader does not start ADC, PWM, LoRa, or issue motor-control commands.

Transport uses MUP1 magic, TARGET=MOTOR_C8 and separate image metadata magic.
The remote's UPD1 client/protocol and flash layout are intentionally incompatible.
Each frame and image has CRC32; startup requires committed metadata and valid
SRAM/reset vectors. Interrupted programming can be retried through bootloader;
there is no dual-slot rollback.

## Package and update

After building both targets:

```powershell
python tools/make_package.py
python tools/test_update.py
```

Outputs: dist/factory.hex for the first ST-Link installation, and
dist/application.bin for subsequent USB-only updates.
Before initial installation, identify the motor MCU/probe, stop the actual motor,
back up its full Flash, and confirm schematic/output levels. Never flash the
remote's factory.hex onto the motor board or vice versa.

After the first installation, with MOTOR_COM replaced by its actual COM port:

```powershell
python tools/usb_update.py dist/application.bin --port COM7 --request
```

Use without --request while already in bootloader recovery mode. Build the
relocated Motor_USB_Application target, not the original LoraDevice_SingleMotor
image at 0x08000000. CubeMX does not manage the custom scatter/VTOR layout.

## Validation

Both named targets built within C8 memory bounds; all eight host tests passed.
Hardware verified: first installation, PING, STATUS, exact 64-byte binary echo,
application entry to bootloader, full USB update/CRC and return to application.
Independent Flash readback matched the factory image and reserved settings page
was unchanged. Actual motor actuation, physical recovery button, active-motor
refusal and power-loss recovery were not exercised. The PC updater checks PONG
MOTOR before requesting a reset, and bootloader TARGET/MUP1 before programming.
