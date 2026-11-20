#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H
#include "stdint.h"
#include "communicationLora.h"

typedef void (*CmdHandler_t)(LoraPackage_t fwPackage);
void CmdHandler_MotorConfig_Load(void);
void CmdHandler_Motor_StatusSent(uint8_t success);
void CmdHandler_Motor_AutoStatusTick(uint32_t now);
typedef struct {
    uint8_t cmdCode;
    CmdHandler_t handler;
} CmdMapping_t;

extern const CmdMapping_t cmdTable[10];

#endif
