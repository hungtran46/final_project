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

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CMD_BUFFER_SIZE 64
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint16_t cmd_index = 0;

volatile float usb_setpoint = 0.0f;
volatile float usb_kp = 350.0f;
volatile float usb_ki = 150.0f;
volatile float usb_kd = 15.0f;
volatile uint8_t usb_update_sp_flag = 0;  // Cờ cập nhật setpoint
volatile uint8_t usb_update_pid_flag = 0; // Cờ cập nhật hệ số PID
volatile uint8_t usb_reset_pid_flag = 0; // Cờ yêu cầu reset tích phân PID
volatile uint8_t usb_save_flash_flag = 0;  // Cờ yêu cầu lưu thông số PID vào Flash
volatile uint8_t usb_send_pid_flag = 0;   // Cờ yêu cầu gửi thông số PID hiện tại lên App

extern volatile uint8_t motor_running;

static void Process_USB_Command(char *cmd)
{
    extern volatile uint8_t usb_beep_flag;
    usb_beep_flag = 1;
    // Cú pháp lệnh từ App:
    // START\n      -> Chạy động cơ
    // STOP\n       -> Dừng động cơ
    // SP:<val>\n   -> Cài đặt Setpoint (apply ngay)
    // KP:<val>\n   -> Buffer Kp (chờ APPLY_PID)
    // KI:<val>\n   -> Buffer Ki (chờ APPLY_PID)
    // KD:<val>\n   -> Buffer Kd (chờ APPLY_PID)
    // APPLY_PID\n  -> Apply KP/KI/KD đồng thời + reset integral
    // RESET_PID\n  -> Chỉ reset tích phân, giữ nguyên gains
    // GET_PID\n    -> Yêu cầu gửi lại thông số PID hiện tại
    if (strcmp(cmd, "START") == 0)
    {
        motor_running = 1;
    }
    else if (strcmp(cmd, "STOP") == 0)
    {
        motor_running = 0;
    }
    else if (strncmp(cmd, "SP:", 3) == 0)
    {
        usb_setpoint = atof(cmd + 3);
        usb_update_sp_flag = 1;
    }
    else if (strncmp(cmd, "KP:", 3) == 0)
    {
        // Chỉ lưu pending, KHÔNG set update_flag
        // Sẽ được apply đồng thời khi nhận APPLY_PID
        usb_kp = atof(cmd + 3);
    }
    else if (strncmp(cmd, "KI:", 3) == 0)
    {
        usb_ki = atof(cmd + 3);
    }
    else if (strncmp(cmd, "KD:", 3) == 0)
    {
        usb_kd = atof(cmd + 3);
    }
    else if (strcmp(cmd, "APPLY_PID") == 0)
    {
        // Apply toàn bộ KP/KI/KD cùng lúc + reset tích phân
        usb_update_pid_flag = 1;
        usb_reset_pid_flag = 1;
        usb_save_flash_flag = 1; // Yêu cầu lưu vào Flash
    }
    else if (strcmp(cmd, "RESET_PID") == 0)
    {
        // Chỉ reset tích phân, không thay đổi gains
        usb_reset_pid_flag = 1;
    }
    else if (strcmp(cmd, "GET_PID") == 0)
    {
        usb_send_pid_flag = 1;
    }
}
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
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);

  // Nhận byte-by-byte và ghép vào cmd_buffer
  for (uint32_t i = 0; i < *Len; i++)
  {
      char c = (char)Buf[i];
      if (c == '\n' || c == '\r')
      {
          if (cmd_index > 0)
          {
              cmd_buffer[cmd_index] = '\0';
              Process_USB_Command(cmd_buffer);
              cmd_index = 0;
          }
      }
      else
      {
          if (cmd_index < CMD_BUFFER_SIZE - 1)
          {
              cmd_buffer[cmd_index++] = c;
          }
          else
          {
              cmd_index = 0; // Tràn bộ đệm, reset
          }
      }
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
  if (hcdc->TxState != 0){
    return USBD_BUSY;
  }
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  /* USER CODE END 7 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
