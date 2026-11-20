"""Explicit diagnostic of existing built scheduler; known defects must FAIL.

No board access or firmware edits. ON states and RTC are simulated in Unicorn.
"""
import struct
import unittest
import test_persistence as harness
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_LR)


class ScheduleChecks(harness.FirmwareHarness):
    def setUp(self):
        super().setUp()
        self.set("netId", 1)
        self.set("nodeId", 1)
        self.set("powerState", 1)
        self.set("sysState", 2)
        self.set("currentTime", 1000, 4)
        self.now = 1000
        self.responses = []
        names = ["osMessageQueuePut", "getUnixTimestamp", "MotorUSB_Process",
                 "HAL_RTC_GetTime", "HAL_RTC_GetDate", "HAL_GPIO_WritePin", "osDelay",
                 "MX_USB_DEVICE_Init", "osMutexAcquire", "osMutexRelease",
                 "HJ_Append", "MotorHistory_Tick"]
        hooks = {harness.symbol(name)[0] & ~1: name for name in names}

        def intercept(cpu, address, size, data):
            if address not in hooks:
                return
            name = hooks[address]
            if name == "osMessageQueuePut":
                self.responses.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 270)))
            if name == "osDelay":
                cpu.reg_write(UC_ARM_REG_PC, 0x0801FFF0)
                return
            cpu.reg_write(UC_ARM_REG_R0, self.now if name == "getUnixTimestamp" else 1 if name == "HJ_Append" else 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        self.cpu.hook_add(UC_HOOK_CODE, intercept)

    def request(self, when, module=0, net=1, node=1, truncated=False):
        packet = bytearray(270)
        packet[:9] = struct.pack("<HBBBBHB", 0xFF55, 16, net, node, 1, 42, 3)
        packet[10:14] = struct.pack("<I", 18 if truncated else 19)
        packet[14:23] = packet[:9]
        packet[23:32] = struct.pack("<BII", module, 1000, when)
        # LoraPackage_t is passed BY VALUE: R0..R3 then the caller's stack.
        stack, stop = 0x2000E000, 0x0801FFF0
        self.cpu.mem_write(stack, bytes(packet[16:]))
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3),
                              struct.unpack("<4I", packet[:16])):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, stack)
        self.cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        self.cpu.emu_start(harness.symbol("handleSetSchedule")[0] | 1, stop, count=200000)
        self.assertEqual(self.cpu.reg_read(UC_ARM_REG_PC), stop)

    def result(self):
        return self.responses[-1][24]  # raw[header 9 + result offset 1]

    def test_first_schedule_after_on_is_actually_stored(self):
        self.set("scheduleTime", 0, 4)
        self.request(1060)
        self.assertEqual(self.result(), 1)
        self.assertEqual(self.get("scheduleTime", 4), 1060,
                         "Slave ACKs success but discards a fresh timer when old schedule is zero")

    def test_replacing_existing_schedule(self):
        self.set("scheduleTime", 1090, 4)
        self.request(1060)
        self.assertEqual(self.result(), 1)
        self.assertEqual(self.get("scheduleTime", 4), 1060)

    def test_cancel_existing_timer(self):
        self.set("scheduleTime", 1060, 4)
        self.request(0)
        self.assertEqual(self.result(), 1)
        self.assertEqual(self.get("scheduleTime", 4), 0)

    def test_future_schedule_is_rejected_when_motor_is_off(self):
        self.set("powerState", 2)
        self.request(1060)
        self.assertEqual(self.result(), 0)
        self.assertEqual(self.get("scheduleTime", 4), 0)

    def test_wrong_module_is_rejected(self):
        self.set("scheduleTime", 1090, 4)
        self.request(1060, module=1)
        self.assertEqual(self.result(), 0)
        self.assertEqual(self.get("scheduleTime", 4), 1090)

    def test_wrong_address_is_ignored(self):
        for address in [(2, 1), (1, 2)]:
            self.request(1060, net=address[0], node=address[1])
        self.assertFalse(self.responses)

    def test_truncated_payload_is_ignored(self):
        self.request(1060, truncated=True)
        self.assertFalse(self.responses)

    def test_does_not_stop_before_deadline(self):
        self.set("scheduleTime", 1001, 4)
        self.call("StartSystemTask")
        self.assertEqual(self.get("powerState"), 1)

    def test_stops_at_deadline_for_an_existing_nonzero_schedule(self):
        self.set("scheduleTime", 1000, 4)
        self.call("StartSystemTask")
        self.assertEqual(self.get("powerState"), 2)
        self.assertEqual(self.get("stopTime", 4), 1000)

    def test_expired_timer_is_cleared_after_auto_stop(self):
        self.set("scheduleTime", 1000, 4)
        self.call("StartSystemTask")
        self.assertEqual(self.get("powerState"), 2)
        self.assertEqual(self.get("scheduleTime", 4), 0,
                         "Expired timer remains in STATUS and the remote UI")


if __name__ == "__main__":
    unittest.main(verbosity=2)
