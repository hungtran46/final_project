#ifndef __PID_H
#define __PID_H

#include "stm32f1xx_hal.h"

// Cấu trúc lưu trữ trạng thái bộ điều khiển PID cải tiến
typedef struct {
    // Các hệ số PID
    float Kp;
    float Ki;
    float Kd;

    // Tham số lọc thông thấp cho khâu vi phân
    float T_f; // Hằng số thời gian lọc (Filter time constant, ví dụ 0.05s)

    // Trạng thái điều khiển
    float setpoint;
    float integral;
    float last_feedback;
    float last_filtered_derivative;

    // Giới hạn ngõ ra (Bảo vệ phần cứng và chống bão hòa tích phân)
    float out_min;
    float out_max;
} PID_HandleTypeDef;

// Khởi tạo bộ PID cải tiến
// T_f: Hằng số bộ lọc vi phân (thông số đề xuất: từ 0.02s đến 0.1s. 0.0f nếu không muốn lọc)
void PID_Init(PID_HandleTypeDef *pid, float Kp, float Ki, float Kd, float T_f, float out_min, float out_max);

// Reset trạng thái tích lũy của PID
void PID_Reset(PID_HandleTypeDef *pid);

// Tinh chỉnh tham số PID thời gian thực
void PID_SetTunings(PID_HandleTypeDef *pid, float Kp, float Ki, float Kd);

// Thay đổi hằng số lọc vi phân
void PID_SetFilterTimeConstant(PID_HandleTypeDef *pid, float T_f);

// Tính toán giá trị ngõ ra PID cải tiến
// feedback: Tốc độ thực tế hiện tại
// dt: Khoảng thời gian lấy mẫu (giây)
float PID_Compute(PID_HandleTypeDef *pid, float feedback, float dt);

#endif /* __PID_H */
