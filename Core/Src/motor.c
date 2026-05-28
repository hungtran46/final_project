#include "motor.h"

static TIM_HandleTypeDef *g_htim = NULL;

void Motor_Init(TIM_HandleTypeDef *htim)
{
    g_htim = htim;

    // Mặc định dừng motor ban đầu
    HAL_GPIO_WritePin(MOTOR_IN1_PORT, MOTOR_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_IN2_PORT, MOTOR_IN2_PIN, GPIO_PIN_RESET);

    // Bắt đầu phát xung PWM trên TIM1 Channel 2
    if (g_htim != NULL)
    {
        HAL_TIM_PWM_Start(g_htim, TIM_CHANNEL_2);
        __HAL_TIM_SET_COMPARE(g_htim, TIM_CHANNEL_2, 0);
    }
}

void Motor_SetSpeed(int32_t speed)
{
    if (g_htim == NULL) return;

    // Giới hạn giá trị speed trong khoảng [-65535, 65535]
    if (speed > 65535) speed = 65535;
    else if (speed < -65535) speed = -65535;

    uint32_t duty = 0;

    if (speed > 0)
    {
        // Quay thuận: IN1 = HIGH, IN2 = LOW
        HAL_GPIO_WritePin(MOTOR_IN1_PORT, MOTOR_IN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOTOR_IN2_PORT, MOTOR_IN2_PIN, GPIO_PIN_RESET);
        duty = (uint32_t)speed;
    }
    else if (speed < 0)
    {
        // Quay ngược: IN1 = LOW, IN2 = HIGH
        HAL_GPIO_WritePin(MOTOR_IN1_PORT, MOTOR_IN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN2_PORT, MOTOR_IN2_PIN, GPIO_PIN_SET);
        duty = (uint32_t)(-speed);
    }
    else
    {
        // Dừng động cơ: IN1 = LOW, IN2 = LOW
        HAL_GPIO_WritePin(MOTOR_IN1_PORT, MOTOR_IN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOTOR_IN2_PORT, MOTOR_IN2_PIN, GPIO_PIN_RESET);
        duty = 0;
    }

    // Set giá trị so sánh (compare register) để thay đổi duty cycle của TIM1 Channel 2
    __HAL_TIM_SET_COMPARE(g_htim, TIM_CHANNEL_2, duty);
}
