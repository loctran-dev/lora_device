"""Run slave status clock sync on built ARM code. No board I/O or power commands."""
import datetime
import struct
import unittest
import test_persistence as h
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC,UC_ARM_REG_LR,UC_ARM_REG_SP

class StatusClockTests(h.FirmwareHarness):
    def setUp(self):
        super().setUp()
        self.now=1795046400
        self.events=[];self.responses=[];self.fail_time=False
        self.set("netId",1);self.set("nodeId",1)
        self.set("currentTime",self.now-20,4)
        self.set("powerState",2)
        hooks={h.symbol(n)[0]&~1:n for n in ["vTaskSuspendAll","xTaskResumeAll","setRTC_dateTime",
               "HAL_RTC_GetTime","HAL_RTC_GetDate","osMessageQueuePut","xTaskGetTickCount"]}
        def intercept(cpu,address,size,data):
            if address not in hooks:return
            name=hooks[address];self.events.append(name)
            result=0
            date=datetime.datetime.fromtimestamp(self.now,datetime.timezone.utc)
            if name=="HAL_RTC_GetTime":
                cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1),bytes([date.hour,date.minute,date.second]))
                result=int(self.fail_time)
            if name=="HAL_RTC_GetDate":
                cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1),bytes([date.isoweekday(),date.month,date.day,date.year-2000]))
            if name=="osMessageQueuePut":
                self.responses.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),270)))
            cpu.reg_write(UC_ARM_REG_R0,result);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        self.cpu.hook_add(UC_HOOK_CODE,intercept)
    def sync(self,target):return self.call("syncStatusTime",target)
    def test_off_clock_and_status_cache_update_immediately(self):
        self.assertEqual(self.sync(self.now+3600),1)
        self.assertEqual(self.get("currentTime",4),self.now+3600)
        self.assertEqual(self.get("powerState"),2)
        self.assertEqual(self.events.count("setRTC_dateTime"),1)
        self.assertEqual((self.events[0],self.events[-1]),("vTaskSuspendAll","xTaskResumeAll"))
    def test_on_without_timer_is_not_turned_off(self):
        self.set("powerState",1)
        self.assertEqual(self.sync(self.now+86400),1)
        self.assertEqual(self.get("powerState"),1)
        self.assertEqual(self.get("scheduleTime",4),0)
    def test_forward_jump_preserves_timer_remaining_from_fresh_rtc(self):
        self.set("powerState",1);self.set("scheduleTime",self.now+60,4)
        self.assertEqual(self.sync(self.now+86400),1)
        self.assertEqual(self.get("scheduleTime",4)-self.get("currentTime",4),60)
        self.assertEqual(self.get("powerState"),1)
    def test_backward_jump_preserves_remaining(self):
        self.set("powerState",1);self.set("scheduleTime",self.now+60,4)
        self.assertEqual(self.sync(self.now-86400),1)
        self.assertEqual(self.get("scheduleTime",4),self.now-86400+60)
    def test_expired_timer_remains_due_not_extended(self):
        self.set("powerState",1);self.set("scheduleTime",self.now-1,4)
        self.sync(self.now+86400)
        self.assertEqual(self.get("scheduleTime",4),self.get("currentTime",4))
    def test_unset_or_out_of_range_remote_clock_is_ignored(self):
        for target in [0,946684800,1704067199,4102444800,0xffffffff]:
            self.assertEqual(self.sync(target),0)
        self.assertEqual(self.events,[])
    def test_small_difference_does_not_write_or_move_timer(self):
        self.set("powerState",1);self.set("scheduleTime",self.now+60,4)
        self.sync(self.now+10)
        self.assertNotIn("setRTC_dateTime",self.events)
        self.assertEqual(self.get("scheduleTime",4),self.now+60)
        self.assertEqual(self.get("currentTime",4),self.now)
    def test_failed_rtc_read_resumes_scheduler_without_mutation(self):
        self.fail_time=True
        self.assertEqual(self.sync(self.now+3600),0)
        self.assertNotIn("setRTC_dateTime",self.events)
        self.assertEqual(self.events[-1],"xTaskResumeAll")
    def test_overflow_of_rebased_deadline_is_rejected(self):
        self.set("powerState",1);self.set("scheduleTime",0xffffffff,4)
        self.assertEqual(self.sync(4102444799),0)
        self.assertNotIn("setRTC_dateTime",self.events)
        self.assertEqual(self.get("scheduleTime",4),0xffffffff)
    def request(self,stamp,module=0,kind=1):
        packet=bytearray(270)
        packet[:9]=struct.pack("<HBBBBHB",0xff55,12,1,1,kind,42,1)
        packet[10:14]=struct.pack("<I",15)
        packet[14:23]=packet[:9]
        packet[23:28]=struct.pack("<BI",module,stamp)
        stack,stop=0x2000e000,0x0801fff0
        self.cpu.mem_write(stack,bytes(packet[16:]))
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),struct.unpack("<4I",packet[:16])):
            self.cpu.reg_write(reg,value)
        self.cpu.reg_write(UC_ARM_REG_SP,stack);self.cpu.reg_write(UC_ARM_REG_LR,stop|1)
        self.cpu.emu_start(h.symbol("handleGetStatus")[0]|1,stop,count=200000)
        self.assertEqual(self.cpu.reg_read(UC_ARM_REG_PC),stop)
    def test_status_reply_contains_new_time_and_rebased_deadline(self):
        self.set("powerState",1);self.set("scheduleTime",self.now+60,4)
        self.request(self.now+86400)
        raw=self.responses[-1][14:]
        # Payload is packed: module/result, phase currents[3], power, local time/deadline.
        self.assertEqual(struct.unpack_from("<II",raw,9+9),(self.now+86400,self.now+86400+60))
        self.assertEqual(struct.unpack_from("<H",raw,6)[0],42)
    def test_all_modules_request_gets_one_record_per_motor(self):
        # moduleIndex 0xFF (LORA_MODULE_ALL): marker, local time, count, then one record.
        self.set("powerState",1);self.set("scheduleTime",self.now+60,4)
        self.request(self.now+86400,module=0xFF)
        raw=self.responses[-1][14:]
        self.assertEqual(struct.unpack_from("<H",raw,6)[0],42)                 # same transaction
        self.assertEqual(struct.unpack_from("<BIB",raw,9),(0xFF,self.now+86400,1))
        self.assertEqual(struct.unpack_from("<BBB",raw,15),(0,1,1))            # module 0, valid, ON
        self.assertEqual(struct.unpack_from("<I",raw,15+9)[0],self.now+86400+60)
        self.assertEqual(raw[2],9-2+6+13)                                      # length excludes the signature
        self.assertIn("setRTC_dateTime",self.events)                           # clock sync still applied
    def test_wrong_module_or_response_does_not_set_clock_or_reply(self):
        self.request(self.now+86400,module=1)
        self.request(self.now+86400,kind=2)
        self.assertEqual(self.responses,[])
        self.assertNotIn("setRTC_dateTime",self.events)

if __name__=="__main__":unittest.main(verbosity=2)
