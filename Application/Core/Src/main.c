/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "commandHandlers.h"
#include "motor_history.h"
#include "usbd_cdc_if.h"
#include "Lora.h"
#include "communicationLora.h"
#include "define.h"
#include "utility.h"
#include "button.h"
#include "math.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim3;

/* Definitions for systemTask */
osThreadId_t systemTaskHandle;
const osThreadAttr_t systemTask_attributes = {
  .name = "systemTask",
  .stack_size = 384 * 4, /* Journal commit and packet building exceed the old 512 B. */
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for sensorTask */
osThreadId_t sensorTaskHandle;
const osThreadAttr_t sensorTask_attributes = {
  .name = "sensorTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for loraSendTask */
osThreadId_t loraSendTaskHandle;
const osThreadAttr_t loraSendTask_attributes = {
  .name = "loraSendTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for loraReceiveTask */
osThreadId_t loraReceiveTaskHandle;
const osThreadAttr_t loraReceiveTask_attributes = {
  .name = "loraReceiveTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for txMsgQueue */
osMessageQueueId_t txMsgQueueHandle;
const osMessageQueueAttr_t txMsgQueue_attributes = {
  .name = "txMsgQueue"
};
/* Definitions for PeriodicSendStatusTimer */
osTimerId_t PeriodicSendStatusTimerHandle;
const osTimerAttr_t PeriodicSendStatusTimer_attributes = {
  .name = "PeriodicSendStatusTimer"
};
/* USER CODE BEGIN PV */
LoRa sLoRaCtrl;
SystemInfo_t sysInfo;
static osMutexId_t motorRadioMutex;
FactoryInfo_t factoryInfo;
Button_t configButton = {BTN1_GPIO_Port, BTN1_Pin, IDLE, 0, 0, 0};
Button_t confirmButton = {BTN2_GPIO_Port, BTN2_Pin, IDLE, 0, 0, 0};
uint16_t errorCode = 0;
uint8_t rxBuffer[128];
uint8_t hasRecieveMsg = FALSE;
const int32_t ADC2CurrentConst = 3.3f / (4096.0f * GAIN_AV * R_SHUNT) * 1000.0f;

 float rms;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_RTC_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM3_Init(void);
void StartSystemTask(void *argument);
void StartSensorTask(void *argument);
void StartLoraSendTask(void *argument);
void StartLoraReceiveTask(void *argument);
void PeriodicSendStatusTimerCallback(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  SCB->VTOR = 0x08000000u; /* ST-Link standalone application, no bootloader. */
  __DSB(); __ISB();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_RTC_Init();
  MX_SPI1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

	// MODULE SETTINGS ----------------------------------------------
	sysInfo.sysState = SYSSTATE_IDLE;
	sysInfo.netIdConfig = 0x01;
	sysInfo.nodeIdConfig = 0x01;
	sysInfo.frequencyConfig = 433;
	sysInfo.sfConfig = SF_7;
	sysInfo.autoStatusSeconds = 60;
	MotorConfig_Load();
    sysInfo.powerState=MOTOR_POWER_OFF;
    MotorHistory_Init();
	sysInfo.netId = sysInfo.netIdConfig;
	sysInfo.nodeId = sysInfo.nodeIdConfig;
	sLoRaCtrl = newLoRa();
	
	sLoRaCtrl.hSPIx                 = &hspi1;
	sLoRaCtrl.CS_port               = LORA_NSS_GPIO_Port;
	sLoRaCtrl.CS_pin                = LORA_NSS_Pin;
	sLoRaCtrl.reset_port            = LORA_RST_GPIO_Port;
	sLoRaCtrl.reset_pin             = LORA_RST_Pin;
	sLoRaCtrl.DIO0_port						 	= LORA_DIO0_GPIO_Port;
	sLoRaCtrl.DIO0_pin							= LORA_DIO0_Pin;
	
	sLoRaCtrl.frequency             = sysInfo.frequencyConfig;
	sLoRaCtrl.spredingFactor        = sysInfo.sfConfig;
	sLoRaCtrl.bandWidth			       	= BW_125KHz;				  // default = BW_125KHz
	sLoRaCtrl.crcRate				      	= CR_4_5;						// default = CR_4_5
	sLoRaCtrl.power					       	= POWER_17db;				// default = 20db
	sLoRaCtrl.overCurrentProtection = 120; 							// default = 100 mA
	sLoRaCtrl.preamble				      = 10;	
	
	LoRa_reset(&sLoRaCtrl);
	errorCode = LoRa_init(&sLoRaCtrl);
	// START CONTINUOUS RECEIVING -----------------------------------
	LoRa_startReceiving(&sLoRaCtrl);
	

	factoryInfo.serial = 0xABCDEF01;
	factoryInfo.deviceType = 0x01;
	factoryInfo.numberOfModule = 1U;
	
	HAL_ADC_Start_DMA(&hadc1, (uint32_t *)sysInfo.phaseCurrentADC, 192);
	__HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);
	HAL_TIM_Base_Start(&htim3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
    const osMutexAttr_t radioAttr={.name="Radio",.attr_bits=osMutexPrioInherit};
    motorRadioMutex=osMutexNew(&radioAttr);
    if (!motorRadioMutex) Error_Handler();
    MotorHistory_Start();
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  /* creation of PeriodicSendStatusTimer */
  PeriodicSendStatusTimerHandle = osTimerNew(PeriodicSendStatusTimerCallback, osTimerPeriodic, NULL, &PeriodicSendStatusTimer_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of txMsgQueue */
  txMsgQueueHandle = osMessageQueueNew (8, sizeof(LoraPackage_t), &txMsgQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of systemTask */
  systemTaskHandle = osThreadNew(StartSystemTask, NULL, &systemTask_attributes);

  /* creation of sensorTask */
  sensorTaskHandle = osThreadNew(StartSensorTask, NULL, &sensorTask_attributes);

  /* creation of loraSendTask */
  loraSendTaskHandle = osThreadNew(StartLoraSendTask, NULL, &loraSendTask_attributes);

  /* creation of loraReceiveTask */
  loraReceiveTaskHandle = osThreadNew(StartLoraReceiveTask, NULL, &loraReceiveTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_USB;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 3;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_ALARM;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
	if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) != 0x3210) 
	{
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_SATURDAY;
  DateToUpdate.Month = RTC_MONTH_JANUARY;
  DateToUpdate.Date = 1;
  DateToUpdate.Year = 0;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x3210);
	}
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 22499;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_OC1;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 11249;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LORA_NSS_Pin|LORA_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED2_Pin|LED1_Pin|DO1_Pin|DO2_Pin
                          |DO3_Pin|DO4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_CTRL_GPIO_Port, USB_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LORA_NSS_Pin LORA_RST_Pin LED2_Pin LED1_Pin
                           DO1_Pin DO2_Pin DO3_Pin DO4_Pin */
  GPIO_InitStruct.Pin = LORA_NSS_Pin|LORA_RST_Pin|LED2_Pin|LED1_Pin
                          |DO1_Pin|DO2_Pin|DO3_Pin|DO4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : LORA_DIO0_Pin LORA_DIO1_Pin */
  GPIO_InitStruct.Pin = LORA_DIO0_Pin|LORA_DIO1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN2_Pin BTN1_Pin DI2_Pin DI3_Pin
                           DI4_Pin */
  GPIO_InitStruct.Pin = BTN2_Pin|BTN1_Pin|DI2_Pin|DI3_Pin
                          |DI4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_CTRL_Pin */
  GPIO_InitStruct.Pin = USB_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DI1_Pin */
  GPIO_InitStruct.Pin = DI1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DI1_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* Preserve the existing LOW-current alarm; zero threshold disables it. */
__attribute__((noinline)) uint8_t Motor_CurrentAlarmDue(uint32_t now)
{
	float threshold = (float)sysInfo.alarmThresholdCentiAmp * 0.01f;
	if (sysInfo.powerState != MOTOR_POWER_ON || !sysInfo.alarmThresholdCentiAmp ||
		!(sysInfo.phaseCurrent[0] < threshold || sysInfo.phaseCurrent[1] < threshold ||
		  sysInfo.phaseCurrent[2] < threshold)) {
		sysInfo.currentDebounceTick = now;
		sysInfo.isAlarmOccurred = FALSE;
		return FALSE;
	}
	if (!sysInfo.isAlarmOccurred && (uint32_t)(now - sysInfo.currentDebounceTick) >=
		(uint32_t)sysInfo.alarmDelaySeconds * configTICK_RATE_HZ) {
		sysInfo.isAlarmOccurred = TRUE;
		sysInfo.lastAlarmTick = now;
		return TRUE;
	}
	return FALSE;
}

__attribute__((noinline)) static void Motor_SendCurrentAlarm(void)
{
	PayloadRequestAlarm_t payload;
	payload.moduleIndex = 0;
	payload.alarmCode = sysInfo.alarmCode;
	payload.alarmTime = sysInfo.currentTime;
	LoraPackage_t packet = createPackageRequestAlarmMsg(sysInfo.netId, sysInfo.nodeId, payload);
	osMessageQueuePut(txMsgQueueHandle, &packet, 0, pdMS_TO_TICKS(100));
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == sLoRaCtrl.DIO0_pin) 
    {			
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(loraReceiveTaskHandle, &xHigherPriorityTaskWoken);
        
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
}

void setRTC_dateTime(RTC_TimeTypeDef sTime, RTC_DateTypeDef sDate) {
	if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
	{
		Error_Handler();
	}
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x3210);
}

void ADC_endConversionIRQHandler(void)
{
	int64_t sumSqrt[3];
	for (int i = 0; i < 64; i++)
	{		
		sumSqrt[0] = sysInfo.phaseCurrentADC[i]*sysInfo.phaseCurrentADC[i] - sysInfo.offsetCurrent[0];
		sumSqrt[1] = sysInfo.phaseCurrentADC[i]*sysInfo.phaseCurrentADC[i] - sysInfo.offsetCurrent[1];
		sumSqrt[2] = sysInfo.phaseCurrentADC[i]*sysInfo.phaseCurrentADC[i] - sysInfo.offsetCurrent[2];
	}
	sysInfo.phaseCurrent[0] = sqrt(sumSqrt[0]) * ADC2CurrentConst;
	sysInfo.phaseCurrent[1] = sqrt(sumSqrt[1]) * ADC2CurrentConst;
	sysInfo.phaseCurrent[2] = sqrt(sumSqrt[2]) * ADC2CurrentConst;
}


/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartSystemTask */
/**
  * @brief  Function implementing the systemTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSystemTask */
void StartSystemTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {	
    MotorUSB_Process();
		//Read rtc time
		HAL_RTC_GetTime(&hrtc, &sysInfo.rtcTime, RTC_FORMAT_BIN);
		HAL_RTC_GetDate(&hrtc, &sysInfo.rtcDate, RTC_FORMAT_BIN);
		sysInfo.currentTime = getUnixTimestamp(&sysInfo.rtcTime, &sysInfo.rtcDate);
		
		if (configButton.isLongPressed == TRUE) {
			configButton.isLongPressed = FALSE;
			
			sysInfo.sysState = SYSSTATE_CONFIGURATION;
			
			sysInfo.netId = LORA_NET_ID_CONFIG;
			sysInfo.nodeId = LORA_NODE_ID_CONFIG;
			sLoRaCtrl.frequency             = 433;							  
			sLoRaCtrl.spredingFactor        = SF_7;		
			LoRa_reset(&sLoRaCtrl);				
			LoRa_init(&sLoRaCtrl);
			LoRa_startReceiving(&sLoRaCtrl);
		}
		
		if (confirmButton.isLongPressed == TRUE) {
			confirmButton.isLongPressed = FALSE;
			
			sysInfo.sysState = SYSSTATE_RUNNING;
			
			sysInfo.netId = sysInfo.netIdConfig;
			sysInfo.nodeId = sysInfo.nodeIdConfig;
			sLoRaCtrl.frequency             = sysInfo.frequencyConfig;							  
			sLoRaCtrl.spredingFactor        = sysInfo.sfConfig;	
			LoRa_reset(&sLoRaCtrl);				
			LoRa_init(&sLoRaCtrl);
			LoRa_startReceiving(&sLoRaCtrl);
		}
		
		if (sysInfo.sysState == SYSSTATE_IDLE) {
			sysInfo.sysState = SYSSTATE_RUNNING;
			sysInfo.currentDebounceTick = xTaskGetTickCount();
			sysInfo.lastAlarmTick = xTaskGetTickCount();
			for (int blinkTime = 0; blinkTime < 3; blinkTime ++) {
				HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
				osDelay(1000);
			}
		}
		else if (sysInfo.sysState == SYSSTATE_RUNNING) {
			HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
			// Check motor scheduler
			if (sysInfo.powerState == MOTOR_POWER_ON 
					&& sysInfo.currentTime >= sysInfo.scheduleTime 
					&& sysInfo.scheduleTime != 0)
			{
                Motor_SetPower(MOTOR_POWER_OFF,HIST_TIMER);
			}
            uint8_t alarmBefore=sysInfo.isAlarmOccurred;
			if (Motor_CurrentAlarmDue(xTaskGetTickCount())) {
                MotorHistory_Record(HIST_ALARM,HIST_SLAVE,sysInfo.powerState,sysInfo.powerState,
                    HIST_ERROR_LOW_CURRENT,HIST_WARNING);
				Motor_SendCurrentAlarm();
			}
            else if (alarmBefore && !sysInfo.isAlarmOccurred)
                MotorHistory_Record(HIST_CLEAR,HIST_SLAVE,sysInfo.powerState,sysInfo.powerState,
                    HIST_ERROR_LOW_CURRENT,HIST_APPLIED);
			
			if (sysInfo.isAlarmOccurred == TRUE) 
			{
				if (xTaskGetTickCount() - sysInfo.lastAlarmTick > pdMS_TO_TICKS(15000)) 
				{
					sysInfo.lastAlarmTick = xTaskGetTickCount();
					
					Motor_SendCurrentAlarm();
				}
			}
			
		}
		else if (sysInfo.sysState == SYSSTATE_CONFIGURATION) {
			HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
			osDelay(200);
		}
		if (hasRecieveMsg == TRUE) {
			hasRecieveMsg = FALSE;
			for (int blinkTime = 0; blinkTime < 3; blinkTime ++) {
				HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
				osDelay(200);
			}
		}
        MotorHistory_Tick();
		osDelay(100);
    
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the sensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
  /* USER CODE BEGIN StartSensorTask */
  /* Infinite loop */
  for(;;)
  {
		// Update button state
		updateButtonStatus(&configButton);
		updateButtonStatus(&confirmButton);
    osDelay(10);
  }
  /* USER CODE END StartSensorTask */
}

/* USER CODE BEGIN Header_StartLoraSendTask */
/**
* @brief Function implementing the loraSendTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLoraSendTask */
void StartLoraSendTask(void *argument)
{
  /* USER CODE BEGIN StartLoraSendTask */
	osStatus_t queueStatus;
	LoraPackage_t txPackage;
	sysInfo.lastStatusTxTick = xTaskGetTickCount() - (uint32_t)sysInfo.autoStatusSeconds * configTICK_RATE_HZ;
	osTimerStart(PeriodicSendStatusTimerHandle, configTICK_RATE_HZ);
  /* Infinite loop */
  for(;;)
  {
		queueStatus = osMessageQueueGet(txMsgQueueHandle, &txPackage, NULL, osWaitForever); 
		if (queueStatus == osOK) {
            if (!MotorHistory_ShouldTransmit(&txPackage)) {
                MotorHistory_TxSkipped(&txPackage); continue;
            }
            osMutexAcquire(motorRadioMutex,osWaitForever);
			uint8_t sent = LoRa_transmit(&sLoRaCtrl, txPackage.rawData, txPackage.byteCount, 5000);
			LoRa_startReceiving(&sLoRaCtrl);
            osMutexRelease(motorRadioMutex);
            MotorHistory_TxFinished(&txPackage);
			if (txPackage.code == CMD_GET_STATUS) Motor_StatusSent(sent);
			osDelay(pdMS_TO_TICKS(200));
		}
  }
  /* USER CODE END StartLoraSendTask */
}

/* USER CODE BEGIN Header_StartLoraReceiveTask */
/**
* @brief Function implementing the loraReceiveTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLoraReceiveTask */
void StartLoraReceiveTask(void *argument)
{
  /* USER CODE BEGIN StartLoraReceiveTask */
	uint32_t ulNotificationValue;
  /* Infinite loop */
  for(;;)
  {
    ulNotificationValue = ulTaskNotifyTake(pdTRUE, osWaitForever);
		if (ulNotificationValue > 0)
		{
            osMutexAcquire(motorRadioMutex,osWaitForever);
			uint8_t rxLength = LoRa_receive(&sLoRaCtrl, rxBuffer, 128);
			processRxData(rxBuffer, rxLength);
            osMutexRelease(motorRadioMutex);
		}
  }
  /* USER CODE END StartLoraReceiveTask */
}

/* PeriodicSendStatusTimerCallback function */
void PeriodicSendStatusTimerCallback(void *argument)
{
  /* USER CODE BEGIN PeriodicSendStatusTimerCallback */
	Motor_AutoStatusTick(xTaskGetTickCount());
  /* USER CODE END PeriodicSendStatusTimerCallback */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
