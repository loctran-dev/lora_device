/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v2.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include "main.h"
#include "define.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "button.h"
#include "motor_history.h"
#include "cmsis_os.h"
extern Button_t configButton, confirmButton;
extern SystemInfo_t sysInfo;
extern osThreadId_t systemTaskHandle,loraReceiveTaskHandle;

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint16_t receivedLength;
static uint8_t replyPending, resetPending;
static uint16_t replyLength;
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:

    break;

    case CDC_GET_LINE_CODING:

    break;

    case CDC_SET_CONTROL_LINE_STATE:

    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  if (*Len == 0)
  {
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  else if (*Len <= APP_RX_DATA_SIZE)
  {
    if (Buf != UserRxBufferFS) memcpy(UserRxBufferFS, Buf, *Len);
    __DMB();
    receivedLength = *Len;
  }
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 7 */
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
  if (hcdc == NULL || hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;
  if (hcdc->TxState != 0){
    return USBD_BUSY;
  }
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  /* USER CODE END 7 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
static void MotorUSB_Button(uint16_t length)
{
  char command[64];
  if (length >= sizeof(command) || UserRxBufferFS[length - 1] != '\n')
  {
    replyLength = sprintf((char*)UserTxBufferFS, "ERR BTN one line required\n");
    return;
  }
  memcpy(command, UserRxBufferFS, length);
  command[--length] = '\0';
  if (length && command[length - 1] == '\r') command[--length] = '\0';
  char *action = strchr(command + 4, ' ');
  if (!action) goto syntax;
  *action++ = '\0';
  Button_t *button;
  if (!strcmp(command + 4, "BTN1") || !strcmp(command + 4, "CONFIG")) button = &configButton;
  else if (!strcmp(command + 4, "BTN2") || !strcmp(command + 4, "CONFIRM")) button = &confirmButton;
  else goto syntax;
  if (!strcmp(action, "CLICK")) button->isPressed = 1;
  else if (!strcmp(action, "LONG")) button->isLongPressed = 1;
  else goto syntax;
  replyLength = sprintf((char*)UserTxBufferFS, "OK BTN %s %s\n",
    button == &configButton ? "BTN1" : "BTN2", action);
  return;
syntax:
  replyLength = sprintf((char*)UserTxBufferFS, "ERR BTN BTN1|BTN2 CLICK|LONG\n");
}

void MotorUSB_Process(void)
{
  USBD_CDC_HandleTypeDef *cdc = hUsbDeviceFS.pClassData;
  if (!cdc || hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
  {
    receivedLength = replyPending = resetPending = 0;
    return;
  }
  if (replyPending)
  {
    if (replyPending == 1)
    {
      if (CDC_Transmit_FS(UserTxBufferFS, replyLength) == USBD_OK) replyPending = 2;
    }
    else if (cdc->TxState == 0)
    {
      replyPending = 0;
      if (resetPending)
      {
        taskENTER_CRITICAL();
        if (sysInfo.powerState == MOTOR_POWER_ON ||
            (GPIOB->ODR & (DO1_Pin | DO2_Pin | DO3_Pin | DO4_Pin)))
        {
          taskEXIT_CRITICAL();
          resetPending = 0;
          replyLength = sprintf((char*)UserTxBufferFS, "ERR MOTOR_OR_OUTPUT_ACTIVE\n");
          replyPending = 1;
          return;
        }
        __HAL_RCC_PWR_CLK_ENABLE();
        __HAL_RCC_BKP_CLK_ENABLE();
        HAL_PWR_EnableBkUpAccess();
        BKP->DR10 = 0xB007u;
        NVIC_SystemReset();
      }
      receivedLength = 0;
      USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
      USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    }
    return;
  }
  uint16_t length = receivedLength;
  if (!length) return;
  if (length == 5 && memcmp(UserRxBufferFS, "PING\n", 5) == 0)
    replyLength = sprintf((char*)UserTxBufferFS, "PONG MOTOR\n");
  else if (length >= 4 && memcmp(UserRxBufferFS, "BTN ", 4) == 0)
    MotorUSB_Button(length);
  else if (length == 5 && memcmp(UserRxBufferFS, "MODE\n", 5) == 0)
    replyLength = sprintf((char*)UserTxBufferFS, "MODE=%u NET=%u NODE=%u I=%u D=%u F=%u SF=%u A=%u\n",
      sysInfo.sysState, sysInfo.netId, sysInfo.nodeId, sysInfo.alarmThresholdCentiAmp,
      sysInfo.alarmDelaySeconds, sysInfo.frequencyConfig, sysInfo.sfConfig, sysInfo.autoStatusSeconds);
  else if (length == 7 && memcmp(UserRxBufferFS, "STATUS\n", 7) == 0)
  {
    replyLength = snprintf((char*)UserTxBufferFS, APP_TX_DATA_SIZE,
      "STATUS PWR=%u A_MA=%ld B_MA=%ld C_MA=%ld\n", sysInfo.powerState,
      (long)(sysInfo.phaseCurrent[0] * 1000), (long)(sysInfo.phaseCurrent[1] * 1000),
      (long)(sysInfo.phaseCurrent[2] * 1000));
    if (replyLength >= APP_TX_DATA_SIZE) replyLength = APP_TX_DATA_SIZE - 1;
  }
  else if (length == 5 && memcmp(UserRxBufferFS, "TIME\n", 5) == 0)
  {
    replyLength=snprintf((char*)UserTxBufferFS,APP_TX_DATA_SIZE,
      "TIME TS=%lu DATE=%04u-%02u-%02u CLOCK=%02u:%02u:%02u DEADLINE=%lu PWR=%u\n",
      (unsigned long)sysInfo.currentTime,2000u+sysInfo.rtcDate.Year,sysInfo.rtcDate.Month,
      sysInfo.rtcDate.Date,sysInfo.rtcTime.Hours,sysInfo.rtcTime.Minutes,sysInfo.rtcTime.Seconds,
      (unsigned long)sysInfo.scheduleTime,sysInfo.powerState);
  }
  else if (length == 8 && memcmp(UserRxBufferFS, "HISTORY\n", 8) == 0)
  {
    unsigned count=0,pending=0,last=0;
    for (uint16_t i=0;i<HJ_SLOTS;i++) {
      const HistoryRecord *r=HJ_Get(i);
      if (!r) continue;
      count++; if (r->synced) pending++;
      if (r->event.id>last) last=r->event.id;
    }
    replyLength=snprintf((char*)UserTxBufferFS,APP_TX_DATA_SIZE,
      "HISTORY COUNT=%u PENDING=%u LAST=%u SENT=%lu ACK=%lu DROP=%lu ERR=%lu FULL=%lu HEAP=%u SYS=%u RX=%u\n",
      count,pending,last,(unsigned long)motorHistorySent,(unsigned long)motorHistoryAcked,
      (unsigned long)motorHistoryDropped,(unsigned long)historyStoreErrors,(unsigned long)historyStoreFull,
      (unsigned)xPortGetFreeHeapSize(),(unsigned)uxTaskGetStackHighWaterMark(systemTaskHandle)*4,
      (unsigned)uxTaskGetStackHighWaterMark(loraReceiveTaskHandle)*4);
  }
  else if (length == 11 && memcmp(UserRxBufferFS, "BOOTLOADER\n", 11) == 0)
  {
    replyLength = sprintf((char*)UserTxBufferFS, "ERR STLINK_ONLY\n");
  }
  else
  {
    memcpy(UserTxBufferFS, UserRxBufferFS, length);
    replyLength = length;
  }
  if (replyLength>=APP_TX_DATA_SIZE) { replyLength=APP_TX_DATA_SIZE-1; UserTxBufferFS[replyLength-1]='\n'; }
  replyPending = 1;
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
