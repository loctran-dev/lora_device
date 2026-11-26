"""Built ARM code tests: autonomous status, legacy journal, and RTC calendar."""
import datetime
import struct
import unittest
import test_persistence as harness
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_PC, UC_ARM_REG_LR

class AutoStatusTests(harness.FirmwareHarness):
    def setUp(self):
        super().setUp()
        self.set("sysState", 2)
        self.set("netId", 1)
        self.set("nodeId", 5)
        self.set("autoStatusSeconds", 2, 2)
        self.now = 0
        self.packets = []
        self.queue_ok = True
        self.complete_before_put_returns = False
        def queue(cpu, address, size, data):
            if self.queue_ok:
                self.packets.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 270)))
                if self.complete_before_put_returns:
                    # Model a higher-priority sender finishing before the timer resumes.
                    cpu.mem_write(harness.symbol("statusQueued")[0], b"\x00")
            cpu.reg_write(UC_ARM_REG_R0, 0 if self.queue_ok else 0xFFFFFFFD)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        def tick(cpu, address, size, data):
            cpu.reg_write(UC_ARM_REG_R0, self.now)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        for name, callback in [("osMessageQueuePut", queue), ("xTaskGetTickCount", tick)]:
            address = harness.symbol(name)[0] & ~1
            self.cpu.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)

    def test_period_counts_from_successful_status_transmission(self):
        self.call("CmdHandler_Motor_AutoStatusTick", 1999)
        self.assertFalse(self.packets)
        self.call("CmdHandler_Motor_AutoStatusTick", 2000)
        self.assertEqual(len(self.packets), 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 5000)
        self.assertEqual(len(self.packets), 1)  # Already queued, not transmitted yet.
        self.now = 5000
        self.call("CmdHandler_Motor_StatusSent", 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 6999)
        self.assertEqual(len(self.packets), 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 7000)
        self.assertEqual(len(self.packets), 2)

    def test_status_wire_address_type_and_checksum(self):
        self.call("CmdHandler_Motor_AutoStatusTick", 2000)
        packet = self.packets[0]
        count = int.from_bytes(packet[10:14], "little")
        wire = packet[14:14 + count]
        self.assertEqual(wire[:2], b"\x55\xFF")
        self.assertEqual(wire[3:6], bytes([1, 5, 2]))  # NET, NODE, RESPONSE
        self.assertEqual(wire[6:9], bytes([0, 0, 1]))  # Unsolicited transaction 0, status opcode.
        # Autonomous reports use the LORA_MODULE_ALL layout: marker, local time, count, records.
        self.assertEqual(wire[9], 0xFF)
        self.assertEqual(wire[14], 1)                    # one motor
        self.assertEqual(wire[15:17], b"\x00\x01")       # module 0, result 1
        self.assertEqual(len(wire), 9 + 6 + 13 + 1)      # header, all-status header, record, CRC
        self.assertEqual(wire[-1], (-sum(wire[:wire[2]])) & 255)

    def test_sender_completion_before_queue_returns_is_not_lost(self):
        self.complete_before_put_returns = True
        self.call("CmdHandler_Motor_AutoStatusTick", 2000)
        self.call("CmdHandler_Motor_AutoStatusTick", 4000)
        self.assertEqual(len(self.packets), 2)

    def test_solicited_status_tx_also_resets_period(self):
        request = bytearray(270)
        request[:9] = struct.pack("<HBBBBHB", 0xFF55, 12, 1, 5, 1, 123, 1)
        self.cpu.mem_write(0x20008800, bytes(request))
        self.call("queueStatus", 0x20008800, 0)
        self.assertEqual(self.packets[-1][6:8], struct.pack("<H", 123))
        self.now = 1500
        self.call("CmdHandler_Motor_StatusSent", 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 3499)
        self.assertEqual(len(self.packets), 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 3500)
        self.assertEqual(len(self.packets), 2)

    def test_zero_disables_autonomous_transmission(self):
        self.set("autoStatusSeconds", 0, 2)
        self.call("CmdHandler_Motor_AutoStatusTick", 999999)
        self.assertFalse(self.packets)

    def test_configuration_mode_does_not_push_status(self):
        self.set("sysState", 1)
        self.call("CmdHandler_Motor_AutoStatusTick", 999999)
        self.assertFalse(self.packets)

    def test_failed_radio_tx_does_not_update_last_status_time(self):
        self.call("CmdHandler_Motor_AutoStatusTick", 2000)
        self.now = 3000
        self.call("CmdHandler_Motor_StatusSent", 0)
        self.assertEqual(self.get("lastStatusTxTick", 4), 0)
        self.call("CmdHandler_Motor_AutoStatusTick", 4000)
        self.assertEqual(len(self.packets), 2)

    def test_legacy_record_defaults_and_new_period_survive(self):
        words = [100 | (2 << 16), 433, 1 | (5 << 8) | (7 << 16)]
        prefix = struct.pack("<III", *words)
        legacy = prefix + struct.pack("<II", (-sum(prefix)) & 255, 0x31474643)
        self.cpu.mem_write(0x0800FC00, legacy)
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("autoStatusSeconds", 2), 60)
        self.set("autoStatusSeconds", 3, 2)
        self.assertEqual(self.call("saveMotorConfig"), 1)
        self.assertEqual(bytes(self.cpu.mem_read(0x0800FC00, 20)), legacy)
        self.set("autoStatusSeconds", 0, 2)
        self.call("CmdHandler_MotorConfig_Load")
        self.assertEqual(self.get("autoStatusSeconds", 2), 3)

    def test_calendar_conversion_months_leap_days_and_weekdays(self):
        dates = []
        for year in range(2000, 2100):
            dates += [datetime.datetime(year, 1, 1, tzinfo=datetime.timezone.utc),
                      datetime.datetime(year, 3, 1, 12, 34, 56, tzinfo=datetime.timezone.utc),
                      datetime.datetime(year, 12, 31, 23, 59, 59, tzinfo=datetime.timezone.utc)]
            if year % 4 == 0:
                dates.append(datetime.datetime(year, 2, 29, 1, 2, 3, tzinfo=datetime.timezone.utc))
        time, date = 0x20008000, 0x20008010
        for stamp in dates:
            with self.subTest(date=str(stamp)):
                self.cpu.mem_write(time, bytes([stamp.hour, stamp.minute, stamp.second]))
                self.cpu.mem_write(date, bytes([stamp.isoweekday(), stamp.month, stamp.day, stamp.year - 2000]))
                self.cpu.reg_write(UC_ARM_REG_R1, date)
                epoch = self.call("getUnixTimestamp", time)
                self.assertEqual(epoch, int(stamp.timestamp()))
                self.cpu.mem_write(time, bytes(3))
                self.cpu.mem_write(date, bytes(4))
                self.cpu.reg_write(UC_ARM_REG_R1, time)
                self.cpu.reg_write(UC_ARM_REG_R2, date)
                self.call("convertTimestampToRTC", epoch)
                self.assertEqual(bytes(self.cpu.mem_read(time, 3)), bytes([stamp.hour, stamp.minute, stamp.second]))
                self.assertEqual(bytes(self.cpu.mem_read(date, 4)), bytes([stamp.isoweekday(), stamp.month, stamp.day, stamp.year - 2000]))

if __name__ == "__main__":
    unittest.main(verbosity=2)
