"""Run the actual built low-current alarm logic in ARM emulation; no board I/O."""
import struct
import unittest
import test_persistence as harness


class CurrentAlarmTests(harness.FirmwareHarness):
    def configure(self, threshold=100, delay=2):
        self.set("powerState", 1)
        self.set("alarmThresholdCentiAmp", threshold, 2)
        self.set("alarmDelaySeconds", delay, 2)

    def currents(self, a=2.0, b=2.0, c=2.0):
        self.cpu.mem_write(self.base + harness.member("phaseCurrent"), struct.pack("<3f", a, b, c))

    def due(self, now):
        return self.call("Motor_CurrentAlarmDue", now)

    def test_configured_threshold_and_delay_boundary(self):
        self.configure(100, 2)
        self.currents()
        self.assertEqual(self.due(1000), 0)
        self.currents(b=0.5)
        self.assertEqual(self.due(1500), 0)
        self.assertEqual(self.due(2999), 0)
        self.assertEqual(self.due(3000), 1)
        self.assertEqual(self.get("isAlarmOccurred"), 1)
        self.assertEqual(self.get("lastAlarmTick", 4), 3000)

    def test_each_phase_can_trigger(self):
        self.configure(100, 0)
        for phase in range(3):
            with self.subTest(phase=phase):
                self.set("isAlarmOccurred", 0)
                currents = [2.0, 2.0, 2.0]
                currents[phase] = 0.5
                self.currents(*currents)
                self.assertEqual(self.due(1000), 1)

    def test_equal_or_above_threshold_does_not_trigger(self):
        self.configure(100, 0)
        self.currents(1.0, 1.1, 100.0)
        self.assertEqual(self.due(1000), 0)
        self.assertEqual(self.get("isAlarmOccurred"), 0)

    def test_recovery_resets_delay(self):
        self.configure(100, 2)
        self.currents()
        self.due(1000)
        self.currents(a=0.5)
        self.assertEqual(self.due(1500), 0)
        self.currents()
        self.assertEqual(self.due(2000), 0)
        self.currents(a=0.5)
        self.assertEqual(self.due(3999), 0)
        self.assertEqual(self.due(4000), 1)

    def test_zero_threshold_disables(self):
        self.configure(0, 0)
        self.currents(-1.0, 0.0, 0.0)
        self.set("isAlarmOccurred", 1)
        self.assertEqual(self.due(999999), 0)
        self.assertEqual(self.get("isAlarmOccurred"), 0)

    def test_motor_off_clears_alarm(self):
        self.configure(100, 0)
        self.currents(0.0, 0.0, 0.0)
        self.set("powerState", 2)
        self.set("isAlarmOccurred", 1)
        self.assertEqual(self.due(1000), 0)
        self.assertEqual(self.get("isAlarmOccurred"), 0)

    def test_no_duplicate_initial_event_until_recovery(self):
        self.configure(100, 0)
        self.currents(0.0, 0.0, 0.0)
        self.assertEqual(self.due(1000), 1)
        self.assertEqual(self.due(1001), 0)
        self.currents()
        self.assertEqual(self.due(1002), 0)
        self.currents(0.0, 0.0, 0.0)
        self.assertEqual(self.due(1003), 1)

    def test_max_delay_does_not_overflow(self):
        self.configure(100, 9999)
        self.currents()
        self.due(1000)
        self.currents(0.0, 0.0, 0.0)
        self.assertEqual(self.due(1000 + 9999 * 1000 - 1), 0)
        self.assertEqual(self.due(1000 + 9999 * 1000), 1)

    def test_tick_wraparound(self):
        self.configure(100, 2)
        baseline = 0xFFFFFF00
        self.currents()
        self.due(baseline)
        self.currents(0.0, 0.0, 0.0)
        self.assertEqual(self.due((baseline + 1999) & 0xFFFFFFFF), 0)
        self.assertEqual(self.due((baseline + 2000) & 0xFFFFFFFF), 1)


LOW_CURRENT, CLEARED, CMD_ALARM = 4, 0x80, 0x07       # ALARM_LOW_CURRENT, ALARM_CLEARED, opcode


class AlarmMessageTests(CurrentAlarmTests.__base__):
    """CMD_ALARM_MESSAGE sent by Motor_AlarmTick (remote KI-17/KI-23)."""

    def setUp(self):
        super().setUp()
        from unicorn import UC_HOOK_CODE
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC, UC_ARM_REG_LR
        self.messages, self.history = [], []

        def queue(cpu, address, size, data):
            packet = bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 270))
            wire = packet[14:14 + int.from_bytes(packet[10:14], "little")]
            self.assertEqual(wire[8], CMD_ALARM)
            self.messages.append(struct.unpack("<BBI", wire[9:15]))   # module, code, start time
            cpu.reg_write(UC_ARM_REG_R0, 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        def record(cpu, address, size, data):
            self.history.append(cpu.reg_read(UC_ARM_REG_R0))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        for name, callback in [("osMessageQueuePut", queue), ("MotorHistory_Record", record)]:
            address = harness.symbol(name)[0] & ~1
            self.cpu.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)
        self.set("powerState", 1)
        self.set("alarmThresholdCentiAmp", 100, 2)
        self.set("alarmDelaySeconds", 0, 2)
        self.low(True)

    def low(self, low):
        value = 0.5 if low else 2.0
        self.cpu.mem_write(self.base + harness.member("phaseCurrent"), struct.pack("<3f", value, 2.0, 2.0))

    def tick(self, now, device_time=None):
        if device_time is not None:
            self.set("currentTime", device_time, 4)
        before = len(self.messages)
        self.call("Motor_AlarmTick", now)
        self.sent_at += [now] * (len(self.messages) - before)

    sent_at = None

    def run(self, result=None):
        self.sent_at = []
        return super().run(result)

    def test_alarm_carries_its_code_and_start_time_in_every_repeat(self):
        self.tick(1000, device_time=1800000000)
        self.tick(16001, device_time=1800000015)
        self.tick(31002, device_time=1800000030)
        self.assertEqual(self.messages, [(0, LOW_CURRENT, 1800000000)] * 3)
        self.assertEqual(self.get("alarmCode"), LOW_CURRENT)

    def test_repeat_period_stays_well_below_the_remote_fallback(self):
        for now in range(1000, 77000, 1000):                            # the remote's 75 s fallback window
            self.tick(now)
        gaps = [b - a for a, b in zip(self.sent_at, self.sent_at[1:])]
        self.assertEqual(self.sent_at[0], 1000)
        self.assertGreaterEqual(len(gaps), 4)
        self.assertTrue(all(gap <= 16000 for gap in gaps), gaps)

    def test_clear_is_sent_three_times_a_few_seconds_apart(self):
        self.tick(1000, device_time=1800000000)
        self.low(False)
        for now in range(2000, 30000, 500):
            self.tick(now)
        self.assertEqual(self.messages[1:], [(0, CLEARED, 1800000000)] * 3)
        self.assertEqual(self.sent_at, [1000, 2000, 7000, 12000])
        self.assertEqual(self.get("alarmCode"), 0)
        self.assertEqual(len(self.history), 2)                          # one ALARM and one CLEAR record

    def test_motor_off_also_sends_the_clear(self):
        self.tick(1000)
        self.set("powerState", 2)
        self.tick(2000)
        self.assertEqual(self.messages[-1][1], CLEARED)

    def test_alarm_back_during_the_clear_repeats_stops_them(self):
        self.tick(1000)
        self.low(False)
        self.tick(2000)                                                 # first CLEARED
        self.low(True)
        self.tick(3000, device_time=1800000100)                        # alarm again
        for now in range(4000, 15000, 1000):
            self.tick(now)
        self.assertEqual([m[1] for m in self.messages], [LOW_CURRENT, CLEARED, LOW_CURRENT])
        self.assertEqual(self.messages[-1][2], 1800000100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
