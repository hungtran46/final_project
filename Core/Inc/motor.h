#ifndef __MOTOR_H
#define __MOTOR_H

#include "main.h"

// Định nghĩa các chân điều khiển chiều quay động cơ qua CubeMX labels
#define MOTOR_IN1_PORT      DC_IN1_GPIO_Port
#define MOTOR_IN1_PIN       DC_IN1_Pin
#define MOTOR_IN2_PORT      DC_IN2_GPIO_Port
#define MOTOR_IN2_PIN       DC_IN2_Pin

// Hàm khởi tạo động cơ
void Motor_Init(TIM_HandleTypeDef *htim);

// Hàm điều khiển tốc độ và hướng động cơ.
// Giá trị speed: từ -65535 (quay ngược max) đến 65535 (quay thuận max). 0 là dừng.
void Motor_SetSpeed(int32_t speed);

#endif /* __MOTOR_H */
