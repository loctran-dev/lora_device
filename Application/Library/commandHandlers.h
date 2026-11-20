#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H
#include "stdint.h"
#include "communicationLora.h"

typedef void (*CmdHandler_t)(LoraPackage_t fwPackage);
void handleControlPower(LoraPackage_t fwPackage);
void handleGetStatus(LoraPackage_t fwPackage) ;
void handleSetSchedule(LoraPackage_t fwPackage);
void handleSyncTime(LoraPackage_t fwPackage);
void handleGetFactoryInfo(LoraPackage_t fwPackage);
void handleConfigConnection(LoraPackage_t fwPackage);
void handleAlarmMessage(LoraPackage_t fwPackage);
void handleSetConfig(LoraPackage_t fwPackage);
void MotorConfig_Load(void);
uint8_t MotorConfig_Save(void);
uint8_t Motor_QueueStatus(LoraPackage_t *request);
void Motor_StatusSent(uint8_t success);
void Motor_AutoStatusTick(uint32_t now);
uint8_t Motor_SyncStatusTime(uint32_t timestamp);
typedef struct {
    uint8_t cmdCode;
    CmdHandler_t handler;
} CmdMapping_t;

extern const CmdMapping_t cmdTable[10];

#endif
