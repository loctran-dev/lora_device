"""Exercise the built motor settings journal in ARM emulation; HAL Flash is mocked."""
import os
import re
import struct
import subprocess
import unittest
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                              UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP)

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "Application/MDK-ARM/Motor_USB_Application"
MAP = (BUILD / "Motor_USB_Application.map").read_text()
FROMELF = Path(os.environ["LOCALAPPDATA"]) / "Keil_v5/ARM/ARM_Compiler_5.06u7/bin/fromelf.exe"
DWARF = subprocess.check_output([str(FROMELF), "--text", "-g", str(BUILD / "Motor_USB_Application.axf")], text=True)


def symbol(name):
    match = re.search(r"^\s+" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+)\s+(?:Data|Thumb Code)\s+(\d+)", MAP, re.M)
    assert match, name
    return int(match[1], 16), int(match[2])


def member(name):
    matches = re.findall(r"DW_AT_name " + name + r"\s+[^\n]*\n[^\n]*DW_AT_data_member_location[^\n]*DW_OP_plus_uconst (\d+)", DWARF)
    assert matches, name
    if name in ("netId", "nodeId"):
        # Wire structs also contain these names at offsets 3..5.
        return min(map(int, matches))
    # Alarm fields also occur in the packed wire payload at smaller offsets.
    return max(map(int, matches))


class FirmwareHarness(unittest.TestCase):
    def setUp(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.cpu.mem_map(0x08000000, 0x20000)
        self.cpu.mem_write(0x08000000, (ROOT / "dist/application.bin").read_bytes())
        self.cpu.mem_write(0x0800FC00, b"\xFF" * 1024)
        self.cpu.mem_map(0x20000000, 0x10000)
        self.base = symbol("sysInfo")[0]
        self.calls = []
        self.fail_program = False
        self.fail_erase = False
        self.hal_names = {symbol(name)[0] & ~1: name for name in
                          ["HAL_FLASH_Unlock", "HAL_FLASH_Lock", "HAL_FLASHEx_Erase", "HAL_FLASH_Program"]}
        self.cpu.hook_add(UC_HOOK_CODE, self.hal_hook)
        self.defaults()

    def defaults(self):
        self.set("netIdConfig", 1)
        self.set("nodeIdConfig", 1)
        self.set("sfConfig", 7)
        self.set("frequencyConfig", 433, 4)
        self.set("alarmThresholdCentiAmp", 0, 2)
        self.set("alarmDelaySeconds", 0, 2)
        self.set("powerState", 0)

    def set(self, name, value, size=1):
        self.cpu.mem_write(self.base + member(name), value.to_bytes(size, "little"))

    def get(self, name, size=1):
        return int.from_bytes(self.cpu.mem_read(self.base + member(name), size), "little")

    def hal_hook(self, cpu, address, size, user):
        names = [self.hal_names[address]] if address in self.hal_names else []
        for name in names:
            self.calls.append(name)
            result = 0
            if name == "HAL_FLASHEx_Erase":
                ptr = cpu.reg_read(UC_ARM_REG_R0)
                page, count = struct.unpack("<II", cpu.mem_read(ptr + 8, 8))
                self.assertEqual((page, count), (0x0800FC00, 1))
                result = int(self.fail_erase)
                if not result:
                    cpu.mem_write(page, b"\xFF" * 1024)
            elif name == "HAL_FLASH_Program":
                target = cpu.reg_read(UC_ARM_REG_R1)
                self.assertTrue(0x0800FC00 <= target <= 0x0800FFFC)
                result = int(self.fail_program)
                if not result:
                    value = cpu.reg_read(UC_ARM_REG_R2).to_bytes(4, "little")
                    previous = bytes(cpu.mem_read(target, 4))
                    self.assertTrue(all((old & new) == new for old, new in zip(previous, value)))
                    cpu.mem_write(target, value)
            cpu.reg_write(UC_ARM_REG_R0, result)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
            return

    def call(self, name, argument=None, argument1=None):
        stop = 0x0801FFF0
        self.cpu.reg_write(UC_ARM_REG_SP, 0x2000F000)
        self.cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        if argument is not None:
            self.cpu.reg_write(UC_ARM_REG_R0, argument & 0xFFFFFFFF)
        if argument1 is not None:
            self.cpu.reg_write(UC_ARM_REG_R1, argument1 & 0xFFFFFFFF)
        self.cpu.emu_start(symbol(name)[0] | 1, stop, count=200000)
        self.assertEqual(self.cpu.reg_read(UC_ARM_REG_PC), stop)
        return self.cpu.reg_read(UC_ARM_REG_R0)

class PersistenceTests(FirmwareHarness):
    def test_save_reload_alarm_and_radio(self):
        self.set("alarmThresholdCentiAmp", 1234, 2)
        self.set("alarmDelaySeconds", 30, 2)
        self.set("netIdConfig", 2)
        self.set("nodeIdConfig", 3)
        self.set("frequencyConfig", 434, 4)
        self.set("sfConfig", 8)
        self.assertEqual(self.call("saveMotorConfig"), 1)
        self.defaults()
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("alarmThresholdCentiAmp", 2), 1234)
        self.assertEqual(self.get("alarmDelaySeconds", 2), 30)
        self.assertEqual(self.get("netIdConfig"), 2)
        self.assertEqual(self.get("nodeIdConfig"), 3)
        self.assertEqual(self.get("frequencyConfig", 4), 434)
        self.assertEqual(self.get("sfConfig"), 8)

    def test_empty_keeps_defaults(self):
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("frequencyConfig", 4), 433)
        self.assertEqual(self.get("alarmThresholdCentiAmp", 2), 0)

    def test_identical_save_no_wear(self):
        self.assertEqual(self.call("saveMotorConfig"), 1)
        self.calls.clear()
        self.assertEqual(self.call("saveMotorConfig"), 1)
        self.assertFalse(self.calls)

    def test_corrupt_new_record_falls_back_to_old(self):
        self.set("alarmThresholdCentiAmp", 100, 2)
        self.call("saveMotorConfig")
        self.set("alarmThresholdCentiAmp", 200, 2)
        self.call("saveMotorConfig")
        self.cpu.mem_write(0x0800FC00 + 20, b"\x00")
        self.defaults()
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("alarmThresholdCentiAmp", 2), 100)

    def test_torn_commit_not_loaded_and_next_save_skips_slot(self):
        self.call("saveMotorConfig")
        self.cpu.mem_write(0x0800FC00 + 20, b"\x00" * 16)
        self.set("alarmDelaySeconds", 42, 2)
        self.assertEqual(self.call("saveMotorConfig"), 1)
        self.assertNotEqual(bytes(self.cpu.mem_read(0x0800FC00 + 40 + 16, 4)), b"\xFF" * 4)
        self.defaults()
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("alarmDelaySeconds", 2), 42)

    def test_full_page_erases_only_settings_page(self):
        for value in range(52):
            self.set("alarmThresholdCentiAmp", value, 2)
            self.assertEqual(self.call("saveMotorConfig"), 1)
        self.assertEqual(self.calls.count("HAL_FLASHEx_Erase"), 1)
        self.defaults()
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("alarmThresholdCentiAmp", 2), 51)

    def test_reject_while_motor_on(self):
        self.set("powerState", 1)
        self.assertEqual(self.call("saveMotorConfig"), 0)
        self.assertFalse(self.calls)

    def test_failure_relocks_flash(self):
        self.fail_program = True
        self.assertEqual(self.call("saveMotorConfig"), 0)
        self.assertEqual(self.calls[-1], "HAL_FLASH_Lock")

    def connection_request(self):
        self.set("sysState", 1)
        self.set("netId", 0)
        self.set("nodeId", 0)
        serial = 0xABCDEF01
        self.cpu.mem_write(symbol("factoryInfo")[0], struct.pack("<I", serial))
        body = struct.pack("<IBBIB", serial, 2, 3, 434, 8)
        header = struct.pack("<HBBBBHB", 0xFF55, 7 + len(body), 0, 0, 1, 123, 6)
        wire = header + body + b"\x00"  # Direct handler test; frame CRC is tested elsewhere.
        packet = header + struct.pack("<BI", 0, len(wire)) + wire + bytes(256 - len(wire))
        replies = []
        def queue(cpu, address, size, user):
            replies.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 270)))
            cpu.reg_write(UC_ARM_REG_R0, 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        addr = symbol("osMessageQueuePut")[0] & ~1
        self.cpu.hook_add(UC_HOOK_CODE, queue, begin=addr, end=addr)
        stack = 0x2000E000
        self.cpu.mem_write(stack, packet[16:])
        for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3],
                              struct.unpack("<IIII", packet[:16])):
            self.cpu.reg_write(reg, value)
        stop = 0x0801FFF0
        self.cpu.reg_write(UC_ARM_REG_SP, stack)
        self.cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        self.cpu.emu_start(symbol("handleConfigConnection")[0] | 1, stop, count=200000)
        self.assertEqual(self.cpu.reg_read(UC_ARM_REG_PC), stop)
        self.assertEqual(len(replies), 1)
        return replies[0][14 + 9]

    def test_pairing_ack_after_persistent_commit(self):
        self.assertEqual(self.connection_request(), 1)
        self.defaults()
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("netIdConfig"), 2)
        self.assertEqual(self.get("nodeIdConfig"), 3)
        self.assertEqual(self.get("frequencyConfig", 4), 434)
        self.assertEqual(self.get("sfConfig"), 8)

    def motor_settings(self, threshold, delay, auto):
        self.set("alarmThresholdCentiAmp", threshold, 2)
        self.set("alarmDelaySeconds", delay, 2)
        self.set("autoStatusSeconds", auto, 2)

    def assert_motor_settings(self, threshold, delay, auto):
        self.assertEqual((self.get("alarmThresholdCentiAmp", 2), self.get("alarmDelaySeconds", 2),
                          self.get("autoStatusSeconds", 2)), (threshold, delay, auto))

    def test_pairing_resets_motor_settings_in_the_same_record(self):
        # Pairing starts from the defaults on both sides (remote KI-10).
        self.motor_settings(1234, 30, 2)
        self.assertEqual(self.connection_request(), 1)
        self.assert_motor_settings(0, 0, 15)               # auto status default 15 s
        self.defaults()
        self.motor_settings(77, 77, 77)                    # only the flash record can bring these back
        self.call("CmdHandler_MotorConfig_Load")
        self.assert_motor_settings(0, 0, 15)
        self.assertEqual(self.get("nodeIdConfig"), 3)

    def test_pairing_flash_failure_keeps_motor_settings(self):
        self.motor_settings(1234, 30, 2)
        self.fail_program = True
        self.assertEqual(self.connection_request(), 0)
        self.assert_motor_settings(1234, 30, 2)

    def test_pairing_flash_failure_nack_and_rollback(self):
        self.fail_program = True
        self.assertEqual(self.connection_request(), 0)
        self.assertEqual(self.get("netIdConfig"), 1)
        self.assertEqual(self.get("nodeIdConfig"), 1)
        self.assertEqual(self.get("frequencyConfig", 4), 433)
        self.assertEqual(self.get("sfConfig"), 7)


if __name__ == "__main__":
    unittest.main(verbosity=2)
