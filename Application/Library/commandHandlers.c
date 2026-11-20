#include "commandHandlers.h"
#include "communicationLora.h"
#include "string.h"
#include "utility.h"
#include "main.h"
#include "cmsis_os.h"
#include "define.h"
#include "Lora.h"
#include "motor_history.h"

extern osMessageQueueId_t txMsgQueueHandle;
extern SystemInfo_t sysInfo;
extern FactoryInfo_t factoryInfo;
extern RTC_HandleTypeDef hrtc;

static uint8_t sendResponse(LoraPackage_t *packet, void *payload, uint8_t size)
{
    packet->type = LORA_PACKAGE_TYPE_RESPONSE;
    Comm_UpdatePackagePayload(packet, payload, size);
    return osMessageQueuePut(txMsgQueueHandle, packet, 0, 0) == osOK;
}

/* Last 1 KiB is reserved by both bootloader and app linker layout. */
#define MOTOR_CONFIG_ADDRESS 0x0800FC00u
#define MOTOR_CONFIG_MAGIC 0x31474643u
static uint8_t configValid(const uint32_t *record)
{
    return record[4] == MOTOR_CONFIG_MAGIC &&
        calculateCRC((uint8_t *)record, 16) == 0 &&
        (record[3] <= 255 || ((record[3] >> 24) == 0xA5 && ((record[3] >> 8) & 0xFFFF) <= 9999)) &&
        (record[0] & 0xFFFFu) <= 9999 && (record[0] >> 16) <= 9999 &&
        record[1] >= 137 && record[1] <= 1020 &&
        (record[2] & 255) > 0 && (record[2] & 255) < 255 &&
        ((record[2] >> 8) & 255) > 0 && ((record[2] >> 8) & 255) < 255 &&
        (record[2] >> 16) >= SF_7 && (record[2] >> 16) <= SF_12;
}

void MotorConfig_Load(void)
{
    for (const uint32_t *p = (const uint32_t *)MOTOR_CONFIG_ADDRESS;
         p + 5 <= (const uint32_t *)(MOTOR_CONFIG_ADDRESS + 1024); p += 5) {
        if (!configValid(p)) continue;
        sysInfo.alarmThresholdCentiAmp = p[0];
        sysInfo.alarmDelaySeconds = p[0] >> 16;
        sysInfo.frequencyConfig = p[1];
        sysInfo.netIdConfig = p[2];
        sysInfo.nodeIdConfig = p[2] >> 8;
        sysInfo.sfConfig = p[2] >> 16;
        sysInfo.autoStatusSeconds = p[3] <= 255 ? 60 : (p[3] >> 8) & 0xFFFF;
    }
}

uint8_t MotorConfig_Save(void)
{
    uint32_t record[5], error;
    const uint32_t *last = NULL;
    uint32_t *slot = NULL;
    if (sysInfo.powerState == MOTOR_POWER_ON) return FALSE;
    record[0] = sysInfo.alarmThresholdCentiAmp | ((uint32_t)sysInfo.alarmDelaySeconds << 16);
    record[1] = sysInfo.frequencyConfig;
    record[2] = sysInfo.netIdConfig | ((uint32_t)sysInfo.nodeIdConfig << 8) |
        ((uint32_t)sysInfo.sfConfig << 16);
    record[3] = 0xA5000000u | ((uint32_t)sysInfo.autoStatusSeconds << 8);
    record[3] |= calculateCRC((uint8_t *)record, 16);
    record[4] = MOTOR_CONFIG_MAGIC;
    if (!configValid(record)) return FALSE;
    for (uint32_t *p = (uint32_t *)MOTOR_CONFIG_ADDRESS;
         p + 5 <= (uint32_t *)(MOTOR_CONFIG_ADDRESS + 1024); p += 5) {
        if (configValid(p)) last = p;
        if (!slot && (p[0] & p[1] & p[2] & p[3] & p[4]) == 0xFFFFFFFFu) slot = p;
    }
    if (last && !memcmp(last, record, sizeof(record))) return TRUE;
    HAL_FLASH_Unlock();
    if (!slot) {
        FLASH_EraseInitTypeDef erase = {0};
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = MOTOR_CONFIG_ADDRESS;
        erase.NbPages = 1;
        if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) {
            HAL_FLASH_Lock();
            return FALSE;
        }
        slot = (uint32_t *)MOTOR_CONFIG_ADDRESS;
    }
    for (uint8_t i = 0; i < 5; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t)&slot[i], record[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return FALSE;
        }
    }
    HAL_FLASH_Lock();
    return !memcmp(slot, record, sizeof(record));
}

const CmdMapping_t cmdTable[] = {
    {CMD_HISTORY, MotorHistory_HandleAck},
    {CMD_SET_CONFIG, handleSetConfig},
    {CMD_GET_STATUS, handleGetStatus},
    {CMD_CONTROL_POWER, handleControlPower},
	{CMD_SET_SCHEDULE, handleSetSchedule},
	{CMD_SYNC_TIME, handleSyncTime},
	{CMD_GET_FACTORY_INFO, handleGetFactoryInfo},
	{CMD_CONFIG_CONNECTION, handleConfigConnection},
	{CMD_ALARM_MESSAGE, handleAlarmMessage},
	{0, NULL} 
};

static volatile uint8_t statusQueued;

__attribute__((noinline)) uint8_t Motor_QueueStatus(LoraPackage_t *request)
{
    PayloadResponseGetStatus_t payload;
    LoraPackage_t packet;
    payload.moduleIndex = 0;
    payload.result = 1;
    for (uint8_t i = 0; i < 3; i++)
        payload.phaseCurrent[i] = (uint16_t)(sysInfo.phaseCurrent[i] * 100.0f);
    payload.powerState = sysInfo.powerState;
    payload.localTime = sysInfo.currentTime;
    payload.scheduleTime = sysInfo.scheduleTime;
    if (!request) {
        packet.signature = LORA_PACKAGE_SIGNATURE;
        packet.netId = sysInfo.netId;
        packet.nodeId = sysInfo.nodeId;
        packet.transactionID = 0; /* Unsolicited report, not a reply transaction. */
        packet.code = CMD_GET_STATUS;
        request = &packet;
    }
    /* Queueing may immediately preempt this low-priority timer with the TX
       task. Mark pending BEFORE publishing, otherwise its completion is lost. */
    statusQueued = 1;
    uint8_t queued = sendResponse(request, &payload, sizeof(payload));
    if (!queued) statusQueued = 0;
    return queued;
}

void Motor_StatusSent(uint8_t success)
{
    uint32_t now = xTaskGetTickCount();
    if (success) sysInfo.lastStatusTxTick = now;
    statusQueued = 0;
}

void Motor_AutoStatusTick(uint32_t now)
{
    if (sysInfo.sysState == SYSSTATE_RUNNING && sysInfo.netId && sysInfo.nodeId &&
        sysInfo.autoStatusSeconds && !statusQueued &&
        (uint32_t)(now - sysInfo.lastStatusTxTick) >= (uint32_t)sysInfo.autoStatusSeconds * configTICK_RATE_HZ) {
        Motor_QueueStatus(NULL);
    }
}
void handleSetConfig(LoraPackage_t packet)
{
    PayloadRequestSetConfig_t config;
    PayloadCommonResponse_t reply;
    if (packet.type != LORA_PACKAGE_TYPE_REQUEST ||
        packet.netId != sysInfo.netId || packet.nodeId != sysInfo.nodeId ||
        (packet.byteCount != LORA_PACKAGE_HEADER_SIZE + 5 + LORA_PACKAGE_CRC_SIZE &&
         packet.byteCount != LORA_PACKAGE_HEADER_SIZE + sizeof(config) + LORA_PACKAGE_CRC_SIZE))
        return;
    config.autoStatusSeconds = sysInfo.autoStatusSeconds;
    memcpy(&config, &packet.rawData[LORA_PACKAGE_HEADER_SIZE],
        packet.byteCount - LORA_PACKAGE_HEADER_SIZE - LORA_PACKAGE_CRC_SIZE);
    reply.moduleIndex = config.moduleIndex;
    reply.result = FALSE;
    if (config.moduleIndex == 0 && config.alarmThresholdCentiAmp <= 9999 &&
        config.alarmDelaySeconds <= 9999 && config.autoStatusSeconds <= 9999 &&
        sysInfo.powerState != MOTOR_POWER_ON) {
        uint16_t oldThreshold = sysInfo.alarmThresholdCentiAmp;
        uint16_t oldDelay = sysInfo.alarmDelaySeconds;
        uint16_t oldAuto = sysInfo.autoStatusSeconds;
        sysInfo.alarmThresholdCentiAmp = config.alarmThresholdCentiAmp;
        sysInfo.alarmDelaySeconds = config.alarmDelaySeconds;
        sysInfo.autoStatusSeconds = config.autoStatusSeconds;
        reply.result = MotorConfig_Save();
        if (!reply.result) {
            sysInfo.alarmThresholdCentiAmp = oldThreshold;
            sysInfo.alarmDelaySeconds = oldDelay;
            sysInfo.autoStatusSeconds = oldAuto;
        }
    }
    packet.type = LORA_PACKAGE_TYPE_RESPONSE;
    Comm_UpdatePackagePayload(&packet, (uint8_t *)&reply, sizeof(reply));
    osMessageQueuePut(txMsgQueueHandle, &packet, 0, 0);
}

__attribute__((noinline)) static void syncTimeIfDifferent(uint32_t time)
{
    int64_t diff = (int64_t)time - (int64_t)sysInfo.currentTime;
    if (diff > MAXIMUM_DIFF_TIME || diff < -MAXIMUM_DIFF_TIME) {
        RTC_TimeTypeDef rtcTime;
        RTC_DateTypeDef rtcDate;
        convertTimestampToRTC(time, &rtcTime, &rtcDate);
        setRTC_dateTime(rtcTime, rtcDate);
    }
}

void handleGetStatus(LoraPackage_t fwPackage) {
/* Status payload is shared by solicited replies and autonomous reports. */
	if (fwPackage.type != LORA_PACKAGE_TYPE_REQUEST || (fwPackage.netId != sysInfo.netId) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_BROADCAST) ||
			(fwPackage.code != CMD_GET_STATUS)) {
		return;
	}		
	PayloadRequestGetStatus_t payloadRequest;
	if (Comm_ParsePayloadReqGetStatus(fwPackage, &payloadRequest) == FALSE) {
		return;
	}
	
    if (payloadRequest.moduleIndex != 0) return;
    Motor_SyncStatusTime(payloadRequest.syncTime);
	sysInfo.lastCommTick = xTaskGetTickCount();
	
	
	Motor_QueueStatus(&fwPackage);
}

uint8_t Motor_SyncStatusTime(uint32_t timestamp)
{
    /* Ignore an unset remote RTC. Keep status queries backwards compatible. */
    if (timestamp<1704067200u || timestamp>4102444799u) return FALSE;
    /* System task also reads the RTC and executes the scheduler. Do not let it
       observe a new clock together with an old absolute deadline (or vice versa).
       IRQs stay enabled so RTC/HAL timeout and the RTOS tick still progress. */
    vTaskSuspendAll();
    if (HAL_RTC_GetTime(&hrtc,&sysInfo.rtcTime,RTC_FORMAT_BIN)!=HAL_OK ||
        HAL_RTC_GetDate(&hrtc,&sysInfo.rtcDate,RTC_FORMAT_BIN)!=HAL_OK) {
        xTaskResumeAll(); return FALSE;
    }
    uint32_t before=getUnixTimestamp(&sysInfo.rtcTime,&sysInfo.rtcDate);
    sysInfo.currentTime=before;
    int64_t difference=(int64_t)timestamp-before;
    if (difference>MAXIMUM_DIFF_TIME || difference< -MAXIMUM_DIFF_TIME) {
        uint32_t deadline=sysInfo.scheduleTime;
        if (sysInfo.powerState==MOTOR_POWER_ON && deadline) {
            uint32_t remaining=deadline>before?deadline-before:0;
            if ((uint64_t)timestamp+remaining>0xffffffffu) { xTaskResumeAll(); return FALSE; }
            deadline=timestamp+remaining;
        }
        RTC_TimeTypeDef time={0}; RTC_DateTypeDef date={0};
        convertTimestampToRTC(timestamp,&time,&date);
        setRTC_dateTime(time,date);
        sysInfo.rtcTime=time; sysInfo.rtcDate=date;
        sysInfo.scheduleTime=deadline;
        sysInfo.currentTime=timestamp;
    }
    xTaskResumeAll();
    return TRUE;
}

void handleControlPower(LoraPackage_t fwPackage) {
	if ((fwPackage.netId != sysInfo.netId) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_BROADCAST) ||
			(fwPackage.code != CMD_CONTROL_POWER)) {
		return;
	}

	PayloadRequestCtrlPower_t payloadCtrlPwr;
	if (Comm_ParsePayloadReqCtrlPwr(fwPackage, &payloadCtrlPwr) == FALSE) {
		return;
	}
	
	if (payloadCtrlPwr.moduleIndex == 0x00 && payloadCtrlPwr.powerState == MOTOR_POWER_ON) 	{
        Motor_SetPower(MOTOR_POWER_ON,HIST_REMOTE);
	}
	else if (payloadCtrlPwr.moduleIndex == 0x00 && payloadCtrlPwr.powerState == MOTOR_POWER_OFF) 	{
        Motor_SetPower(MOTOR_POWER_OFF,HIST_REMOTE);
	}
	
	
	PayloadCommonResponse_t payload;
	payload.moduleIndex = 0x00;
	payload.result = 0x01;
	
	sendResponse(&fwPackage, &payload, sizeof(payload));

}


void handleSetSchedule(LoraPackage_t fwPackage) {
	if ((fwPackage.netId != sysInfo.netId) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_BROADCAST) ||
			(fwPackage.code != CMD_SET_SCHEDULE)) {
		return;
	}

	uint8_t result = FALSE;
	PayloadRequestSetSchedule_t payloadRequest;
	if (Comm_ParsePayloadReqSetSchedule(fwPackage, &payloadRequest) == FALSE) {
		return;
	}
	
	if (payloadRequest.moduleIndex == 0x00 && sysInfo.powerState == MOTOR_POWER_ON) {
		if (payloadRequest.scheduleTime < payloadRequest.syncTime || payloadRequest.scheduleTime == 0)
		{
			sysInfo.scheduleTime = 0;
		}
		else
		{
			sysInfo.scheduleTime = payloadRequest.scheduleTime;
		}
		syncTimeIfDifferent(payloadRequest.syncTime);
		result = TRUE;
	}
	
	
	PayloadCommonResponse_t payload;
	payload.moduleIndex = 0x00;
	payload.result = result;
	
	sendResponse(&fwPackage, &payload, sizeof(payload));

}

void handleSyncTime(LoraPackage_t fwPackage) {
	if ((fwPackage.netId != sysInfo.netId) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_BROADCAST) ||
			(fwPackage.code != CMD_SYNC_TIME)) {
		return;
	}

	uint8_t result = FALSE;
	PayloadRequestSyncTime_t payloadRequest;
	if (Comm_ParsePayloadReqSyncTime(fwPackage, &payloadRequest) == FALSE) {
		return;
	}
	
	RTC_TimeTypeDef sTime;
	RTC_DateTypeDef sDate;
	convertTimestampToRTC(payloadRequest.unixTime, &sTime, &sDate);
	setRTC_dateTime(sTime, sDate);
	
	
	PayloadCommonResponse_t payload;
	payload.moduleIndex = 0x00;
	payload.result = result;
	
	sendResponse(&fwPackage, &payload, sizeof(payload));

}
void handleGetFactoryInfo(LoraPackage_t fwPackage){
	if ((fwPackage.netId != sysInfo.netId && fwPackage.netId != LORA_NET_ID_CONFIG) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_CONFIG) ||
			(fwPackage.code != CMD_GET_FACTORY_INFO)) {
		return;
	}
	if (sysInfo.sysState != SYSSTATE_CONFIGURATION) {
		return;
	}

	
	PayloadResponseGetFactoryInfo_t  payload;
	payload.result = 0x01;
	payload.serial = factoryInfo.serial;
	payload.deviceType = factoryInfo.deviceType;
	payload.numberOfModule = factoryInfo.numberOfModule;
	
	sendResponse(&fwPackage, &payload, sizeof(payload));

}
void handleConfigConnection(LoraPackage_t fwPackage) {
	if ((fwPackage.netId != sysInfo.netId && fwPackage.netId != LORA_NET_ID_CONFIG) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_CONFIG) ||
			(fwPackage.code != CMD_CONFIG_CONNECTION)) {
		return;
	}
	if (sysInfo.sysState != SYSSTATE_CONFIGURATION) {
		return;
	}
	uint8_t result = FALSE;
	PayloadRequestConfigConnection_t payloadRequest;
	if (Comm_ParsePayloadReqConfigConnection(fwPackage, &payloadRequest) == TRUE) {
		if (payloadRequest.serial == factoryInfo.serial) {
			uint8_t oldNet = sysInfo.netIdConfig, oldNode = sysInfo.nodeIdConfig, oldSF = sysInfo.sfConfig;
			int oldFrequency = sysInfo.frequencyConfig;
			
			sysInfo.netIdConfig = payloadRequest.netId;
			sysInfo.nodeIdConfig = payloadRequest.nodeId;
			sysInfo.frequencyConfig = payloadRequest.frequency;
			sysInfo.sfConfig = payloadRequest.sf;
			result = MotorConfig_Save();
			if (!result) {
				sysInfo.netIdConfig = oldNet;
				sysInfo.nodeIdConfig = oldNode;
				sysInfo.sfConfig = oldSF;
				sysInfo.frequencyConfig = oldFrequency;
			}
		}
		else {
			return;
		}
	}
	else {
	}
	
	
	PayloadResponseConfigConnection_t payload;
	payload.result = result;
	payload.serial = factoryInfo.serial;
	sendResponse(&fwPackage, &payload, sizeof(payload));

}

void handleAlarmMessage(LoraPackage_t fwPackage)
{
	if ((fwPackage.netId != sysInfo.netId && fwPackage.netId != LORA_NET_ID_CONFIG) ||
			(fwPackage.nodeId != sysInfo.nodeId && fwPackage.nodeId != LORA_NODE_ID_CONFIG) ||
			(fwPackage.code != CMD_ALARM_MESSAGE)) {
		return;
	}
	PayloadCommonResponse_t payloadResponse;
	memcpy(&payloadResponse, &fwPackage.rawData[LORA_PACKAGE_HEADER_SIZE], sizeof(payloadResponse));
}

