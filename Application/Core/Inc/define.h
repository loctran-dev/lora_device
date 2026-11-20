#ifndef __DEFINE_H__
#define __DEFINE_H__

#include "stdint.h"
#include "stm32f1xx_hal.h"

#define MAXIMUM_DEVICE 20

#define MAXIMUM_DIFF_TIME 30


#define GAIN_AV 3.0f
#define R_SHUNT 50.0f
#define V_OFFSET 1.65f

typedef enum {
	ALARM_NONE,
	ALARM_LOST_INPUT_POWER,
	ALARM_LOST_MOTOR_POWER,
	ALARM_DIFF_MOTOR_CURRENT_PHASE,
} AlarmName_t;

typedef enum  {
	SLAVE_TYPE_MOTOR = 0x00,
	SLAVE_TYPE_PRESSURE = 0x01
} SlaveType_t;

typedef enum {
	MOTOR_POWER_IDLE = 0x00,
	MOTOR_POWER_ON = 0x01,
	MOTOR_POWER_OFF = 0x02,
} MotorPowerState_t;

typedef enum {
	SYSSTATE_IDLE = 0x00,
	SYSSTATE_CONFIGURATION,
	SYSSTATE_RUNNING
}SystemState_t;

typedef struct {
	SystemState_t sysState;
	uint8_t netId;
	uint8_t netIdConfig;
	uint8_t nodeId;
	uint8_t nodeIdConfig;
	
	int frequencyConfig;
	uint8_t sfConfig;
	float rsi;
	
	MotorPowerState_t powerState;
	uint16_t phaseCurrentADC[384];
	float phaseCurrent[3];
	int32_t offsetCurrent[3];
	uint32_t currentDebounceTick;
    uint16_t alarmThresholdCentiAmp;
    uint16_t alarmDelaySeconds;
	uint32_t startTime;
	uint32_t stopTime;
	uint32_t scheduleTime;
	uint32_t currentTime;
	
	RTC_TimeTypeDef rtcTime;
	RTC_DateTypeDef rtcDate;
	
	uint8_t isAlarmOccurred;
	uint8_t alarmCode;
	uint32_t alarmTime;
	uint32_t lastAlarmTick;
	uint32_t lastCommTick;
	uint16_t autoStatusSeconds;
	uint32_t lastStatusTxTick;
} SystemInfo_t;

typedef struct {
	uint32_t serial;
	uint8_t deviceType;
	uint8_t numberOfModule;
} FactoryInfo_t;

#endif
