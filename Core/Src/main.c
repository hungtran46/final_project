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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "motor.h"
#include "encoder.h"
#include "pid.h"
#include "i2c_lcd.h"
#include "button.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define FLASH_STORAGE_PAGE_ADDR   0x0800FC00
#define PID_FLASH_MAGIC           0x55AA33CC
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

/* USER CODE BEGIN PV */
typedef struct {
    uint32_t magic;
    float kp;
    float ki;
    float kd;
} PID_Storage;

PID_HandleTypeDef motor_pid;
volatile float actual_rpm = 0.0f;
volatile float target_rpm = 0.0f;
volatile uint8_t motor_running = 0; // 0: Stopped, 1: Running
volatile uint8_t usb_beep_flag = 0; // Cờ phát tiếng bíp khi nhận lệnh USB
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
void Buzzer_Beep(uint32_t duration_ms);
void PID_SaveToFlash(float kp, float ki, float kd);
void PID_LoadFromFlash(float *kp, float *ki, float *kd);
void PID_FormatFloat(char *buf, size_t buf_size, float val);
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
  MX_USB_DEVICE_Init();
  MX_TIM1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  // Khởi động các module khác trước để có thể dùng còi báo hiệu
  Motor_Init(&htim1);
  Encoder_Init();
  Button_Init();

  // Khởi động giao tiếp LCD
  if (LCD_Init(&hi2c1) != HAL_OK)
  {
      // Nếu không tìm thấy LCD, kêu còi 3 tiếng bíp ngắn để báo hiệu lỗi kết nối LCD
      for (int i = 0; i < 3; i++)
      {
          Buzzer_Beep(80);
          HAL_Delay(80);
      }
  }
  else
  {
      // Khởi tạo LCD thành công
      LCD_Clear();
      LCD_SetCursor(0, 0);
      LCD_SendString("PID Controller");
      LCD_SetCursor(1, 0);
      LCD_SendString("Ready!");
      
      // Kêu còi 1 tiếng bíp dài báo hiệu khởi động thành công
      Buzzer_Beep(200);
      HAL_Delay(500);
  }

  // Đọc thông số PID đã lưu trong Flash (nếu có), nếu không dùng mặc định
  float init_kp = 89.9360f;
  float init_ki = 228.4688f;
  float init_kd = 0.0614f;
  PID_LoadFromFlash(&init_kp, &init_ki, &init_kd);

  // Cập nhật giá trị ban đầu cho các biến USB để đồng bộ với App khi kết nối
  extern volatile float usb_kp, usb_ki, usb_kd;
  usb_kp = init_kp;
  usb_ki = init_ki;
  usb_kd = init_kd;

  // Khởi động PID với thông số đã tải
  // T_f=0.05s (= dt=50ms) → alpha=0.5, bộ lọc vi phân cân bằng
  PID_Init(&motor_pid, init_kp, init_ki, init_kd, 0.05f, -65535.0f, 65535.0f);

  LCD_Clear();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_pid_tick = HAL_GetTick();
  uint32_t last_btn_tick = HAL_GetTick();
  uint32_t last_lcd_tick = HAL_GetTick();
  uint32_t last_usb_tick = HAL_GetTick();

  char lcd_buf[20];
  static char usb_buf[64];

  while (1)
  {
      uint32_t current_tick = HAL_GetTick();

      // 1. Quét trạng thái nút bấm mỗi 10ms
      if (current_tick - last_btn_tick >= 10)
      {
          last_btn_tick = current_tick;
          Button_Scan();

          Button_Event btn_evt = Button_GetEvent();
          if (btn_evt == BTN_UP_PRESSED)
          {
              target_rpm += 10.0f;
              if (target_rpm > 200.0f) target_rpm = 200.0f; // Giới hạn trên phù hợp với động cơ 170 RPM
              motor_pid.setpoint = target_rpm;
              Buzzer_Beep(50); // Còi bíp phản hồi ngắn
          }
          else if (btn_evt == BTN_DOWN_PRESSED)
          {
              target_rpm -= 10.0f;
              if (target_rpm < 0.0f) target_rpm = 0.0f; // Không cho Setpoint âm
              motor_pid.setpoint = target_rpm;
              Buzzer_Beep(50); // Còi bíp phản hồi ngắn
          }
          else if (btn_evt == BTN_START_PRESSED)
          {
              motor_running = !motor_running;
              if (!motor_running)
              {
                  Motor_SetSpeed(0);
                  PID_Reset(&motor_pid);
              }
              Buzzer_Beep(150); // Còi bíp dài báo đổi trạng thái chạy/dừng
          }
      }

      // 2. Kiểm tra và nhận dữ liệu điều khiển từ cổng USB CDC
      extern volatile uint8_t usb_reset_pid_flag;
      extern volatile uint8_t usb_save_flash_flag;
      extern volatile uint8_t usb_update_sp_flag;
      extern volatile uint8_t usb_update_pid_flag;

      if (usb_update_sp_flag)
      {
          usb_update_sp_flag = 0;
          target_rpm = usb_setpoint;
          if (target_rpm < 0.0f) target_rpm = 0.0f;
          motor_pid.setpoint = target_rpm;
      }

      if (usb_update_pid_flag)
      {
          usb_update_pid_flag = 0;
          PID_SetTunings(&motor_pid, usb_kp, usb_ki, usb_kd);
      }

      if (usb_reset_pid_flag)
      {
          usb_reset_pid_flag = 0;
          PID_Reset(&motor_pid);  // Xóa tích phân để mỗi lần test PID bắt đầu sạch
      }

      if (usb_save_flash_flag)
      {
          usb_save_flash_flag = 0;
          PID_SaveToFlash(motor_pid.Kp, motor_pid.Ki, motor_pid.Kd);
          Buzzer_Beep(100);  // Kêu bíp ngắn báo hiệu đã lưu thành công vào Flash
      }

      extern volatile uint8_t usb_send_pid_flag;
      static uint32_t last_pid_send_tick = 0;
      if (usb_send_pid_flag && (current_tick - last_pid_send_tick >= 50))
      {
          last_pid_send_tick = current_tick;
          static char tx_buf[64];
          char kp_str[16];
          char ki_str[16];
          char kd_str[16];
          PID_FormatFloat(kp_str, sizeof(kp_str), motor_pid.Kp);
          PID_FormatFloat(ki_str, sizeof(ki_str), motor_pid.Ki);
          PID_FormatFloat(kd_str, sizeof(kd_str), motor_pid.Kd);
          int len = snprintf(tx_buf, sizeof(tx_buf), "PID:%s,%s,%s\n", kp_str, ki_str, kd_str);
          if (CDC_Transmit_FS((uint8_t*)tx_buf, len) == USBD_OK)
          {
              usb_send_pid_flag = 0;
          }
      }

      if (usb_beep_flag)
      {
          usb_beep_flag = 0;
          Buzzer_Beep(50); // Phát tiếng bíp ngắn báo hiệu đã nhận lệnh USB thành công
      }

      // 3. Tính toán điều khiển PID định kỳ mỗi 50ms
      // Lý do: dt=50ms → ~20 pulses tại 30 RPM → sai số lượng tử ±5%
      //        dt=20ms → ~8  pulses tại 30 RPM → sai số lượng tử ±13%
      if (current_tick - last_pid_tick >= 50)
      {
          // Tính toán thời gian thực tế đã trôi qua kể từ lần cập nhật trước
          float dt = (float)(current_tick - last_pid_tick) / 1000.0f;
          last_pid_tick = current_tick;

          // Lấy tốc độ thực tế đo được từ encoder dựa trên dt thực tế
          actual_rpm = Encoder_GetRPM(dt);

          if (motor_running && target_rpm > 0.0f)
          {
              float pid_out = PID_Compute(&motor_pid, actual_rpm, dt);
              Motor_SetSpeed((int32_t)pid_out);
          }
          else
          {
              Motor_SetSpeed(0);
              PID_Reset(&motor_pid);
          }
      }

      // 4. Stream dữ liệu lên máy tính qua USB CDC mỗi 100ms
      if (current_tick - last_usb_tick >= 100)
      {
          last_usb_tick = current_tick;
          
          int32_t target_int = (int32_t)target_rpm;
          int32_t target_frac = (int32_t)((target_rpm - (float)target_int) * 10.0f);
          if (target_frac < 0) target_frac = -target_frac;

          int32_t actual_int = (int32_t)actual_rpm;
          int32_t actual_frac = (int32_t)((actual_rpm - (float)actual_int) * 10.0f);
          if (actual_frac < 0) actual_frac = -actual_frac;

          int len = snprintf(usb_buf, sizeof(usb_buf), "RPM:%ld.%ld,%ld.%ld\n", 
                             (long)target_int, (long)target_frac, 
                             (long)actual_int, (long)actual_frac);
          CDC_Transmit_FS((uint8_t*)usb_buf, len);
      }

      // 5. Cập nhật thông tin hiển thị màn hình LCD I2C mỗi 200ms
      if (current_tick - last_lcd_tick >= 200)
      {
          last_lcd_tick = current_tick;

          // Hàng 1: Setpoint và trạng thái motor
          LCD_SetCursor(0, 0);
          if (motor_running)
          {
              snprintf(lcd_buf, sizeof(lcd_buf), "SP:%4ld [RUN]   ", (long)target_rpm);
          }
          else
          {
              snprintf(lcd_buf, sizeof(lcd_buf), "SP:%4ld [STP]   ", (long)target_rpm);
          }
          LCD_SendString(lcd_buf);

          // Hàng 2: Tốc độ thực tế hiện tại và số xung thô 20ms gần nhất để debug
          LCD_SetCursor(1, 0);
          snprintf(lcd_buf, sizeof(lcd_buf), "PV:%4ld P:%-4ld  ", (long)actual_rpm, (long)Encoder_GetLastPulses());
          LCD_SendString(lcd_buf);
      }

      // 6. Điều khiển nhấp nháy LED trạng thái (PC13 - Onboard LED kích mức LOW)
      if (motor_running)
      {
          // Nhấp nháy chu kỳ 500ms khi chạy (Bật 250ms, Tắt 250ms)
          if ((current_tick / 250) % 2 == 0)
          {
              HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET); // Bật LED
          }
          else
          {
              HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);   // Tắt LED
          }
      }
      else
      {
          // Tắt hẳn LED khi động cơ dừng
          HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
      }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, BUZZER_IN4_Pin|BUZZER_IN3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, DC_IN2_Pin|DC_IN1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BUZZER_IN4_Pin BUZZER_IN3_Pin */
  GPIO_InitStruct.Pin = BUZZER_IN4_Pin|BUZZER_IN3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : DC_IN2_Pin DC_IN1_Pin */
  GPIO_InitStruct.Pin = DC_IN2_Pin|DC_IN1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB3 PB4 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : DOWN_Pin START_STOP_Pin UP_Pin */
  GPIO_InitStruct.Pin = DOWN_Pin|START_STOP_Pin|UP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void Buzzer_Beep(uint32_t duration_ms)
{
    // Bật còi qua Driver L298N Channel B (OUT3 = HIGH, OUT4 = LOW)
    HAL_GPIO_WritePin(BUZZER_IN3_PORT, BUZZER_IN3_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZER_IN4_PORT, BUZZER_IN4_PIN, GPIO_PIN_RESET);
    HAL_Delay(duration_ms);
    // Tắt còi (OUT3 = LOW, OUT4 = LOW)
    HAL_GPIO_WritePin(BUZZER_IN3_PORT, BUZZER_IN3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZER_IN4_PORT, BUZZER_IN4_PIN, GPIO_PIN_RESET);
}

void PID_SaveToFlash(float kp, float ki, float kd)
{
    HAL_FLASH_Unlock();

    // Erase the page first (0x0800FC00 is page 63, the last page of 64KB Flash)
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError = 0;
    memset(&EraseInitStruct, 0, sizeof(EraseInitStruct));
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.PageAddress = FLASH_STORAGE_PAGE_ADDR;
    EraseInitStruct.NbPages = 1;
    EraseInitStruct.Banks = FLASH_BANK_1;

    // Clear any flash flags that could be active
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) == HAL_OK)
    {
        // Program magic number
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_STORAGE_PAGE_ADDR, PID_FLASH_MAGIC);

        // Program Kp (copy to uint32_t to avoid type punning issues)
        uint32_t kp_raw;
        memcpy(&kp_raw, &kp, sizeof(kp));
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_STORAGE_PAGE_ADDR + 4, kp_raw);

        // Program Ki
        uint32_t ki_raw;
        memcpy(&ki_raw, &ki, sizeof(ki));
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_STORAGE_PAGE_ADDR + 8, ki_raw);

        // Program Kd
        uint32_t kd_raw;
        memcpy(&kd_raw, &kd, sizeof(kd));
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_STORAGE_PAGE_ADDR + 12, kd_raw);
    }

    HAL_FLASH_Lock();
}

void PID_LoadFromFlash(float *kp, float *ki, float *kd)
{
    PID_Storage *stored = (PID_Storage*)FLASH_STORAGE_PAGE_ADDR;
    if (stored->magic == PID_FLASH_MAGIC)
    {
        *kp = stored->kp;
        *ki = stored->ki;
        *kd = stored->kd;
    }
    else
    {
        // Default optimal values for dt=50ms
        *kp = 89.9360f;
        *ki = 228.4688f;
        *kd = 0.0614f;
    }
}

void PID_FormatFloat(char *buf, size_t buf_size, float val)
{
    int32_t val_int = (int32_t)val;
    float frac = val - (float)val_int;
    if (frac < 0.0f) frac = -frac;
    int32_t val_frac = (int32_t)(frac * 10000.0f + 0.5f);
    if (val_frac >= 10000)
    {
        val_int += 1;
        val_frac -= 10000;
    }
    snprintf(buf, buf_size, "%ld.%04ld", (long)val_int, (long)val_frac);
}
/* USER CODE END 4 */

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
