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


if __name__ == "__main__":
    unittest.main(verbosity=2)
