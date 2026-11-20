#include "communicationLora.h"
#include "string.h"
#include "utility.h"
#include "Lora.h"
#include "main.h"
#include "commandHandlers.h"
uint8_t nodeid = 0x01;

uint16_t currentTransactionID = 0;

static void executeCommand(LoraPackage_t package);

__attribute__((noinline)) static uint8_t parsePayload(LoraPackage_t *packet, uint8_t code,
                                                      void *payload, uint8_t size)
{
    if (packet->code != code ||
        packet->byteCount != LORA_PACKAGE_HEADER_SIZE + size + LORA_PACKAGE_CRC_SIZE)
        return FALSE;
    memcpy(payload, packet->rawData + LORA_PACKAGE_HEADER_SIZE, size);
    return TRUE;
}
static uint16_t generateTransactionID(void)
{
    currentTransactionID++;
    return currentTransactionID;
}

void Comm_UpdatePackagePayload(LoraPackage_t *packageOut, uint8_t *payload, int16_t payloadLength)
{
		packageOut->length = LORA_PACKAGE_HEADER_SIZE - sizeof(packageOut->signature) + payloadLength;
    memcpy(packageOut->rawData, packageOut, LORA_PACKAGE_HEADER_SIZE);
    memcpy(&packageOut->rawData[LORA_PACKAGE_HEADER_SIZE], payload, payloadLength);
		packageOut->byteCount = LORA_PACKAGE_HEADER_SIZE + payloadLength + sizeof(packageOut->checkSum);
    packageOut->checkSum = calculateCRC(&packageOut->rawData[0], packageOut->length);
    memcpy(&packageOut->rawData[packageOut->byteCount - 1], &packageOut->checkSum, sizeof(packageOut->checkSum));
}

LoraPackage_t Comm_CreateNewPackage(uint8_t netId, uint8_t nodeId, uint8_t opcode, uint8_t type, uint8_t *payload, int16_t payloadLength, uint32_t timeout)
{
    LoraPackage_t packageOut;
    memset(packageOut.rawData, 0x00, LORA_PACKAGE_RAW_DATA_SIZE);
    packageOut.signature = LORA_PACKAGE_SIGNATURE;
    packageOut.length = LORA_PACKAGE_HEADER_SIZE - sizeof(packageOut.signature) + payloadLength;
		packageOut.netId = netId;
		packageOut.nodeId = nodeId;
    packageOut.type = type;
    packageOut.transactionID = generateTransactionID();
    packageOut.code = opcode;
    packageOut.byteCount = LORA_PACKAGE_HEADER_SIZE + payloadLength + sizeof(packageOut.checkSum);

    memcpy(packageOut.rawData, &packageOut, LORA_PACKAGE_HEADER_SIZE);
    memcpy(&packageOut.rawData[LORA_PACKAGE_HEADER_SIZE], payload, payloadLength);

    packageOut.checkSum = calculateCRC(&packageOut.rawData[0], packageOut.length);
    memcpy(&packageOut.rawData[packageOut.byteCount - 1], &packageOut.checkSum, sizeof(packageOut.checkSum));

    return packageOut;
}

LoraPackage_t Comm_CreateReqGetStatus(uint8_t netId, uint8_t nodeId, PayloadResponseGetStatus_t payload)
{
    return Comm_CreateNewPackage(netId, nodeId, CMD_GET_STATUS, LORA_PACKAGE_TYPE_RESPONSE, (uint8_t *)&payload, sizeof(payload), 0);
}

LoraPackage_t Comm_CreateReqAlarmMsg(uint8_t netId, uint8_t nodeId, PayloadRequestAlarm_t payload)
{
	return Comm_CreateNewPackage(netId, nodeId, CMD_ALARM_MESSAGE, LORA_PACKAGE_TYPE_REQUEST, (uint8_t*)&payload, sizeof(payload), 0);
}
void Comm_ProcessRxData(uint8_t *buffer, uint32_t length)
{
		if (length < 3) return;

    uint8_t *frameStart = NULL;
    uint32_t frameLength = 0;

    for (uint32_t i = 0; i < (length - 1); i++) 
		{
        if (*(uint16_t *)&buffer[i] == LORA_PACKAGE_SIGNATURE) 
				{
            frameStart = &buffer[i];      
            frameLength = length - i;    
            break;
        }  
    }
		
		LoraPackage_t fwPackage;
		uint8_t isFrameValid = FALSE;
    if (frameStart != NULL && frameLength >= LORA_PACKAGE_HEADER_SIZE + LORA_PACKAGE_CRC_SIZE) 
		{
				memset(fwPackage.rawData, 0x00, LORA_PACKAGE_RAW_DATA_SIZE);
				memcpy(&fwPackage, frameStart, LORA_PACKAGE_HEADER_SIZE);
			
				fwPackage.byteCount = fwPackage.length + sizeof(fwPackage.signature) + sizeof(fwPackage.checkSum);
			
				if (fwPackage.byteCount > frameLength || fwPackage.byteCount > LORA_PACKAGE_RAW_DATA_SIZE ||
					fwPackage.byteCount < LORA_PACKAGE_HEADER_SIZE + LORA_PACKAGE_CRC_SIZE) return;
				memcpy(fwPackage.rawData, frameStart, fwPackage.byteCount);
				fwPackage.checkSum = frameStart[fwPackage.byteCount - 1];
				uint8_t readCRC = calculateCRC(&fwPackage.rawData[0], fwPackage.length);
				if(readCRC != fwPackage.checkSum) {
						return;
				}
				isFrameValid = TRUE;
				
				executeCommand(fwPackage);
    } 
		else 
		{
				return;
    }
		
		if (isFrameValid == FALSE) {
				return;
		}
}
uint8_t Comm_ParsePayloadReqGetStatus(LoraPackage_t package, PayloadRequestGetStatus_t *payload)
{
    return parsePayload(&package, CMD_GET_STATUS, payload, sizeof(*payload));
}
uint8_t Comm_ParsePayloadReqCtrlPwr(LoraPackage_t package, PayloadRequestCtrlPower_t *payload)
{
    return parsePayload(&package, CMD_CONTROL_POWER, payload, sizeof(*payload));
}
uint8_t Comm_ParsePayloadReqSetSchedule(LoraPackage_t package, PayloadRequestSetSchedule_t *payload)
{
    return parsePayload(&package, CMD_SET_SCHEDULE, payload, sizeof(*payload));
}

uint8_t Comm_ParsePayloadReqSyncTime(LoraPackage_t package, PayloadRequestSyncTime_t *payload)
{
    return parsePayload(&package, CMD_SYNC_TIME, payload, sizeof(*payload));
}
uint8_t Comm_ParsePayloadReqConfigConnection(LoraPackage_t package, PayloadRequestConfigConnection_t *payload)
{
    return parsePayload(&package, CMD_CONFIG_CONNECTION, payload, sizeof(*payload));
}
uint8_t Comm_ParsePayloadReqAlarm(LoraPackage_t package, PayloadRequestAlarm_t *payload)
{
    return parsePayload(&package, CMD_ALARM_MESSAGE, payload, sizeof(*payload));
}

static void executeCommand(LoraPackage_t package) {
    for (int i = 0; cmdTable[i].handler != NULL; i++) {
        if (cmdTable[i].cmdCode == package.code) {
            cmdTable[i].handler(package); 
            return;
        }
    }
}
