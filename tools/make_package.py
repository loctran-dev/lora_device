"""Produce application.bin and factory.hex; never connect to the board."""
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_hex(path):
    memory, upper = {}, 0
    for line in path.read_text().splitlines():
        record = bytes.fromhex(line.strip().removeprefix(":"))
        if not record or sum(record) & 255 or len(record) != record[0] + 5:
            raise ValueError(f"Invalid HEX record in {path}")
        count, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        if kind == 0:
            for index, byte in enumerate(record[4:4 + count]):
                location = upper + address + index
                if location in memory:
                    raise ValueError("Overlapping HEX records")
                memory[location] = byte
        elif kind == 4:
            upper = int.from_bytes(record[4:6], "big") << 16
        elif kind == 2:
            upper = int.from_bytes(record[4:6], "big") << 4
        elif kind == 1:
            break
        elif kind not in (3, 5):
            raise ValueError("Unsupported HEX record")
    if not memory:
        raise ValueError("Empty HEX image")
    return memory


def hex_record(kind, address, payload):
    body = bytes([len(payload)]) + address.to_bytes(2, "big") + bytes([kind]) + payload
    return ":" + (body + bytes([-sum(body) & 255])).hex().upper()


def write_hex(memory, path):
    lines, previous_upper = [], None
    addresses = sorted(memory)
    index = 0
    while index < len(addresses):
        address = addresses[index]
        upper = address >> 16
        if upper != previous_upper:
            lines.append(hex_record(4, 0, upper.to_bytes(2, "big")))
            previous_upper = upper
        block = bytearray([memory[address]])
        index += 1
        while (index < len(addresses) and len(block) < 16 and
               addresses[index] == address + len(block) and addresses[index] >> 16 == upper):
            block.append(memory[addresses[index]])
            index += 1
        lines.append(hex_record(0, address & 65535, block))
    lines.append(hex_record(1, 0, b""))
    path.write_text("\n".join(lines) + "\n")


def main():
    app = read_hex(ROOT / "Application/MDK-ARM/Motor_USB_Application/Motor_USB_Application.hex")
    if min(app) != 0x08000000 or max(app) >= 0x0800DC00:
        raise ValueError("ST-Link app crosses reserved history/config storage")
    image = bytes(app.get(address, 255) for address in range(0x08000000, max(app) + 1))
    stack, reset = struct.unpack_from("<II", image)
    if not (0x20000000 < stack <= 0x20005000 and stack % 8 == 0 and
            reset & 1 and 0x08000000 <= (reset & ~1) < 0x08000000 + len(image)):
        raise ValueError("Invalid standalone app vectors")
    output = ROOT / "dist"
    output.mkdir(exist_ok=True)
    legacy=output/"legacy-usb-factory.hex"
    current=output/"factory.hex"
    if current.exists() and not legacy.exists():
        old=read_hex(current)
        if 0x0800F800 in old: write_hex(old,legacy)
    (output / "application.bin").write_bytes(image)
    write_hex(app, output / "application.hex")
    write_hex(app, output / "factory.hex")
    print(f"Application: {len(image)} bytes, CRC32={zlib.crc32(image):08X}")
    print("ST-LINK ONLY, app at 08000000; history DC00..FBFF and config FC00..FFFF absent")
    print("Created application.bin, application.hex, factory.hex (standalone); USB recovery is legacy-usb-factory.hex")


if __name__ == "__main__":
    main()
