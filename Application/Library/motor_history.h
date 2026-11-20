#ifndef MOTOR_HISTORY_H
#define MOTOR_HISTORY_H
#include "communicationLora.h"
#include "history_journal.h"
void MotorHistory_Init(void);
void MotorHistory_Start(void);
void MotorHistory_Tick(void);
void MotorHistory_TxFinished(const LoraPackage_t *packet);
void MotorHistory_TxSkipped(const LoraPackage_t *packet);
uint8_t MotorHistory_ShouldTransmit(const LoraPackage_t *packet);
void MotorHistory_HandleAck(LoraPackage_t packet);
void MotorHistory_Record(uint8_t action, uint8_t source, uint8_t before, uint8_t after, uint16_t error, uint8_t result);
void Motor_SetPower(uint8_t power, uint8_t source);
extern uint32_t motorHistorySent, motorHistoryAcked, motorHistoryDropped;
#endif
