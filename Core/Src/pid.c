#include "pid.h"

void PID_Init(PID_HandleTypeDef *pid, float Kp, float Ki, float Kd, float T_f, float out_min, float out_max)
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->T_f = T_f;
    pid->out_min = out_min;
    pid->out_max = out_max;

    pid->setpoint = 0.0f;
    PID_Reset(pid);
}

void PID_Reset(PID_HandleTypeDef *pid)
{
    pid->integral = 0.0f;
    pid->last_feedback = 0.0f;
    pid->last_filtered_derivative = 0.0f;
}

void PID_SetTunings(PID_HandleTypeDef *pid, float Kp, float Ki, float Kd)
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
}

void PID_SetFilterTimeConstant(PID_HandleTypeDef *pid, float T_f)
{
    pid->T_f = T_f;
}

float PID_Compute(PID_HandleTypeDef *pid, float feedback, float dt)
{
    if (dt <= 0.0f) return 0.0f;

    // 1. Tính toán sai số hiện tại (error)
    float error = pid->setpoint - feedback;

    // 2. Tính toán khâu vi phân dựa trên sự thay đổi của phản hồi (Derivative-on-Measurement)
    // Việc này loại bỏ hoàn toàn hiện tượng đột biến cơ học "Derivative Kick" khi Setpoint thay đổi đột ngột.
    float d_feedback = feedback - pid->last_feedback;
    float raw_derivative = -d_feedback / dt;
    pid->last_feedback = feedback;

    // Áp dụng bộ lọc thông thấp (Low-pass filter) cho khâu vi phân để khử nhiễu từ tín hiệu của Encoder
    float filtered_derivative = raw_derivative;
    if (pid->T_f > 0.0f)
    {
        float alpha = dt / (pid->T_f + dt);
        filtered_derivative = (alpha * raw_derivative) + ((1.0f - alpha) * pid->last_filtered_derivative);
    }
    pid->last_filtered_derivative = filtered_derivative;

    // 3. Tính toán ngõ ra thô (raw output) trước khi giới hạn
    float proportional_term = pid->Kp * error;
    float derivative_term = pid->Kd * filtered_derivative;
    float output_raw = proportional_term + (pid->Ki * pid->integral) + derivative_term;

    // 4. Giới hạn ngõ ra thực tế (Saturation)
    float output = output_raw;
    if (output > pid->out_max) output = pid->out_max;
    else if (output < pid->out_min) output = pid->out_min;

    // 5. Cập nhật khâu tích phân (Integral) dùng giải thuật chống bão hòa có điều kiện (Conditional Integration/Clamping)
    // Chúng ta KHÔNG tích lũy tích phân nếu:
    // - Ngõ ra thô lớn hơn cực đại (đang bão hòa dương) và sai số vẫn dương (muốn tăng thêm đầu ra)
    // - Ngõ ra thô nhỏ hơn cực tiểu (đang bão hòa âm) và sai số vẫn âm (muốn giảm thêm đầu ra)
    uint8_t clamp_integral = 0;
    if (output_raw > pid->out_max && error > 0.0f)
    {
        clamp_integral = 1;
    }
    else if (output_raw < pid->out_min && error < 0.0f)
    {
        clamp_integral = 1;
    }

    if (!clamp_integral)
    {
        pid->integral += error * dt;
    }

    return output;
}
