"""Built ARM checks of CMD_UNPAIR (remote KI-27): a deleted slave leaves the network."""
import struct
import unittest

import test_persistence as harness
from test_persistence import symbol
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
                              UC_ARM_REG_PC, UC_ARM_REG_LR)

CMD_UNPAIR, REQUEST, RUNNING, ON, OFF = 0x0A, 1, 2, 1, 2


class UnpairTests(harness.FirmwareHarness):
    def setUp(self):
        super().setUp()
        self.set("sysState", RUNNING)
        self.set("netId", 1)
        self.set("nodeId", 5)
        self.set("powerState", ON)
        self.replies, self.power = [], []
        self.now = 1000

        def queue(cpu, address, size, data):
            self.replies.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 270)))
            cpu.reg_write(UC_ARM_REG_R0, 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        def set_power(cpu, address, size, data):
            self.power.append(cpu.reg_read(UC_ARM_REG_R0))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        def tick(cpu, address, size, data):
            cpu.reg_write(UC_ARM_REG_R0, self.now)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        for name, callback in [("osMessageQueuePut", queue), ("Motor_SetPower", set_power),
                               ("xTaskGetTickCount", tick)]:
            address = symbol(name)[0] & ~1
            self.cpu.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)

    def unpair(self, net=1, node=5, kind=REQUEST):
        body = b"\x00"
        header = struct.pack("<HBBBBHB", 0xFF55, 7 + len(body), net, node, kind, 77, CMD_UNPAIR)
        wire = header + body + b"\x00"
        packet = header + struct.pack("<BI", 0, len(wire)) + wire + bytes(256 - len(wire))
        stack, stop = 0x2000E000, 0x0801FFF0
        self.cpu.mem_write(stack, packet[16:])
        for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3],
                              struct.unpack("<IIII", packet[:16])):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, stack)
        self.cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        self.cpu.emu_start(symbol("handleUnpair")[0] | 1, stop, count=200000)
        self.assertEqual(self.cpu.reg_read(UC_ARM_REG_PC), stop)

    def test_unpair_replies_switches_off_and_schedules_configuration_mode(self):
        self.unpair()
        self.assertEqual(len(self.replies), 1)
        wire = self.replies[0][14:]
        self.assertEqual((wire[5], wire[8]), (2, CMD_UNPAIR))      # RESPONSE to CMD_UNPAIR
        self.assertEqual(wire[9:11], b"\x00\x01")                  # module 0, accepted
        self.assertEqual(self.power, [OFF])
        self.assertEqual(self.get("unpairTick", 4), 1001)          # tick | 1: never 0

    def test_motor_already_off_is_not_switched_again(self):
        self.set("powerState", OFF)
        self.unpair()
        self.assertEqual(self.power, [])
        self.assertEqual(len(self.replies), 1)

    def test_only_the_paired_address_is_obeyed(self):
        for args in [dict(node=6), dict(net=2), dict(node=0xFF), dict(kind=2)]:
            self.unpair(**args)
        self.assertEqual((self.replies, self.power, self.get("unpairTick", 4)), ([], [], 0))

    def test_not_in_configuration_mode_and_copies_are_ignored(self):
        self.set("sysState", 1)                                    # pairing mode: not paired traffic
        self.unpair()
        self.assertEqual(self.replies, [])
        self.set("sysState", RUNNING)
        self.unpair()
        self.unpair()                                              # second of the remote's three copies
        self.assertEqual(len(self.replies), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
