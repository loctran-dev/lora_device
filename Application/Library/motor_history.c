#include "motor_history.h"
#include "main.h"
#include "define.h"
#include "cmsis_os.h"
#include <string.h>
extern SystemInfo_t sysInfo;
extern FactoryInfo_t factoryInfo;
extern osMessageQueueId_t txMsgQueueHandle;
static osMutexId_t historyMutex;
static uint8_t queued, haveSent;
static uint32_t lastSent, inFlight;
uint32_t motorHistorySent, motorHistoryAcked, motorHistoryDropped;
void MotorHistory_Init(void)
{
    HJ_Init(); queued=0; haveSent=1; lastSent=HAL_GetTick(); inFlight=0;
}
void MotorHistory_Start(void)
{
    const osMutexAttr_t attr={.name="History",.attr_bits=osMutexPrioInherit};
    historyMutex=osMutexNew(&attr);
    if (!historyMutex) Error_Handler();
}
static void appendEvent(uint8_t action, uint8_t source, uint8_t before, uint8_t after, uint16_t error, uint8_t result)
{
    HistoryEvent event={0};
    event.serial=factoryInfo.serial; event.timestamp=sysInfo.currentTime;
    event.action=action; event.source=source; event.before=before; event.after=after;
    event.error=error; event.result=result;
    if (event.timestamp<1704067200u) event.flags=HIST_TIME_UNKNOWN;
    if (!HJ_Append(&event,0)) motorHistoryDropped++;
}
void MotorHistory_Record(uint8_t action, uint8_t source, uint8_t before, uint8_t after, uint16_t error, uint8_t result)
{
    osMutexAcquire(historyMutex,osWaitForever);
    appendEvent(action,source,before,after,error,result);
    osMutexRelease(historyMutex);
}
/* Future IO handling calls this with HIST_IO1..HIST_IO4. No inputs are enabled here. */
void Motor_SetPower(uint8_t power, uint8_t source)
{
    if (power!=MOTOR_POWER_ON && power!=MOTOR_POWER_OFF) return;
    osMutexAcquire(historyMutex,osWaitForever);
    uint8_t before=sysInfo.powerState;
    HAL_GPIO_WritePin(LED1_GPIO_Port,LED1_Pin,power==MOTOR_POWER_ON?GPIO_PIN_SET:GPIO_PIN_RESET);
    sysInfo.powerState=(MotorPowerState_t)power;
    if (power==MOTOR_POWER_ON) sysInfo.startTime=sysInfo.currentTime;
    else sysInfo.stopTime=sysInfo.currentTime;
    sysInfo.scheduleTime=0;
    appendEvent(power==MOTOR_POWER_ON?HIST_ON:HIST_OFF,source,before,power,
                sysInfo.isAlarmOccurred?HIST_ERROR_LOW_CURRENT:0,
                before==power?HIST_UNCHANGED:HIST_APPLIED);
    osMutexRelease(historyMutex);
}
void MotorHistory_Tick(void)
{
    uint32_t now=HAL_GetTick();
    if (sysInfo.sysState!=SYSSTATE_RUNNING || queued || !sysInfo.netId || !sysInfo.nodeId ||
        (haveSent && (uint32_t)(now-lastSent)<10000u)) return;
    osMutexAcquire(historyMutex,osWaitForever);
    const HistoryRecord *r=HJ_Pending();
    if (r) {
        HistoryPush push={r->event,HJ_Crc(&r->event,sizeof(r->event))};
        LoraPackage_t packet=createNewPackage(sysInfo.netId,sysInfo.nodeId,CMD_HISTORY,
            LORA_PACKAGE_TYPE_REQUEST,(uint8_t *)&push,sizeof(push),0);
        inFlight=r->event.id; queued=1;
        if (osMessageQueuePut(txMsgQueueHandle,&packet,0,0)!=osOK) queued=0;
    }
    osMutexRelease(historyMutex);
}
uint8_t MotorHistory_ShouldTransmit(const LoraPackage_t *packet)
{
    if (packet->code!=CMD_HISTORY) return 1;
    const HistoryRecord *r=HJ_Pending();
    uint32_t id;
    memcpy(&id,packet->rawData+LORA_PACKAGE_HEADER_SIZE+4,4);
    return r && r->event.id==id;
}
void MotorHistory_TxFinished(const LoraPackage_t *packet)
{
    if (packet->code!=CMD_HISTORY) return;
    /* Every physical attempt, including a failed one, starts the 10 s quiet period. */
    lastSent=HAL_GetTick(); haveSent=1; queued=0; motorHistorySent++;
}
void MotorHistory_TxSkipped(const LoraPackage_t *packet)
{
    if (packet->code==CMD_HISTORY) queued=0;
}
void MotorHistory_HandleAck(LoraPackage_t packet)
{
    if (packet.type!=LORA_PACKAGE_TYPE_RESPONSE || packet.netId!=sysInfo.netId ||
        packet.nodeId!=sysInfo.nodeId || packet.byteCount!=LORA_PACKAGE_HEADER_SIZE+sizeof(HistoryAck)+1) return;
    HistoryAck ack;
    memcpy(&ack,packet.rawData+LORA_PACKAGE_HEADER_SIZE,sizeof(ack));
    osMutexAcquire(historyMutex,osWaitForever);
    const HistoryRecord *r=HJ_Pending();
    if (r && ack.serial==r->event.serial && ack.id==r->event.id && ack.id==inFlight &&
        ack.eventCrc==HJ_Crc(&r->event,sizeof(r->event)) && HJ_MarkSynced(ack.serial,ack.id))
        motorHistoryAcked++;
    osMutexRelease(historyMutex);
}
