/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define BUZZER_IN4_Pin GPIO_PIN_14
#define BUZZER_IN4_GPIO_Port GPIOB
#define BUZZER_IN3_Pin GPIO_PIN_15
#define BUZZER_IN3_GPIO_Port GPIOB
#define DC_IN2_Pin GPIO_PIN_8
#define DC_IN2_GPIO_Port GPIOA
#define DC_IN1_Pin GPIO_PIN_10
#define DC_IN1_GPIO_Port GPIOA
#define DOWN_Pin GPIO_PIN_5
#define DOWN_GPIO_Port GPIOB
#define START_STOP_Pin GPIO_PIN_6
#define START_STOP_GPIO_Port GPIOB
#define UP_Pin GPIO_PIN_7
#define UP_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define LED_PIN             LED_Pin
#define LED_PORT            LED_GPIO_Port

#define BUZZER_IN3_PIN      BUZZER_IN3_Pin
#define BUZZER_IN3_PORT     BUZZER_IN3_GPIO_Port
#define BUZZER_IN4_PIN      BUZZER_IN4_Pin
#define BUZZER_IN4_PORT     BUZZER_IN4_GPIO_Port
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
