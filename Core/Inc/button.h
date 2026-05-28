#ifndef __BUTTON_H
#define __BUTTON_H

#include "main.h"

// Định nghĩa các chân kết nối nút bấm
#define BUTTON_PORT         GPIOB
#define BUTTON_UP_PIN       UP_Pin
#define BUTTON_DOWN_PIN     DOWN_Pin
#define BUTTON_START_PIN    START_STOP_Pin

// Định nghĩa loại sự kiện của nút nhấn
typedef enum {
    BTN_NONE = 0,
    BTN_UP_PRESSED,
    BTN_DOWN_PRESSED,
    BTN_START_PRESSED
} Button_Event;

// Khởi tạo các chân GPIO cho nút nhấn
void Button_Init(void);

// Quét trạng thái nút nhấn (Cần được gọi định kỳ, ví dụ mỗi 10ms-20ms)
// Hàm này sẽ tự động chống dội và phát hiện sườn xuống (rơi xuống mức LOW)
void Button_Scan(void);

// Lấy sự kiện nút bấm vừa xảy ra (Hàm trả về sự kiện rồi tự động xóa hàng đợi sự kiện)
Button_Event Button_GetEvent(void);

#endif /* __BUTTON_H */
