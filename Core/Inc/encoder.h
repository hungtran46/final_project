#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f1xx_hal.h"

// Định nghĩa các chân đọc tín hiệu Encoder
#define ENCODER_A_PORT GPIOB
#define ENCODER_A_PIN GPIO_PIN_3 // Sử dụng ngắt ngoài EXTI3 (Chân PB3)
#define ENCODER_B_PORT GPIOB
#define ENCODER_B_PIN GPIO_PIN_4 // Đọc mức logic để biết hướng quay (Chân PB4)

// Định nghĩa hằng số cơ khí của Động cơ (Có thể thay đổi tùy loại động cơ)
#ifndef ENCODER_PPR
#define ENCODER_PPR 11.0f // Số xung trên 1 vòng của đĩa encoder
#endif

#ifndef GEAR_RATIO
#define GEAR_RATIO 35.5f // Tỷ số truyền hộp số (động cơ 1/35.5 170 RPM)
#endif

// Khởi tạo GPIO ngắt ngoài cho Encoder
void Encoder_Init(void);

// Hàm được gọi trong ISR ngắt ngoài EXTI Line 1
void Encoder_Pulse_Callback(void);

// Hàm tính toán và lấy tốc độ thực tế (RPM)
// dt: Khoảng thời gian đo (tính bằng giây), ví dụ 0.05 giây cho chu kỳ 50ms
float Encoder_GetRPM(float dt);

// Reset bộ đếm xung
void Encoder_Reset(void);

// Lấy số xung thô của chu kỳ gần nhất
int32_t Encoder_GetLastPulses(void);

// Hàm trễ microgiây chính xác sử dụng DWT Cycle Counter
void Delay_us(uint32_t us);

#endif /* __ENCODER_H */
