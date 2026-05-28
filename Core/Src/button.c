#include "button.h"

// Biến lưu trữ sự kiện nút bấm hiện tại
static volatile Button_Event g_button_event = BTN_NONE;

// Các cấu trúc lưu trạng thái cho việc chống dội (debouncing)
typedef struct {
    uint16_t pin;
    uint8_t stable_state;
    uint8_t raw_state;
    uint8_t count;
    Button_Event press_event;
} Button_State_t;

// Khai báo 3 nút bấm
static Button_State_t buttons[] = {
    { BUTTON_UP_PIN,    1, 1, 0, BTN_UP_PRESSED },
    { BUTTON_DOWN_PIN,  1, 1, 0, BTN_DOWN_PRESSED },
    { BUTTON_START_PIN, 1, 1, 0, BTN_START_PRESSED }
};

#define NUM_BUTTONS (sizeof(buttons)/sizeof(buttons[0]))

void Button_Init(void)
{
    // Bật clock GPIOB (đã được bật trong main.c)
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = BUTTON_UP_PIN | BUTTON_DOWN_PIN | BUTTON_START_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // Sử dụng điện trở kéo lên nội
    HAL_GPIO_Init(BUTTON_PORT, &GPIO_InitStruct);
}

void Button_Scan(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        // Đọc mức logic thô từ pin (0: Pressed, 1: Released)
        uint8_t pin_val = (uint8_t)HAL_GPIO_ReadPin(BUTTON_PORT, buttons[i].pin);

        if (pin_val == buttons[i].raw_state)
        {
            // Trạng thái thô đang ổn định
            if (buttons[i].count < 3) // 3 chu kỳ quét ổn định (ví dụ 30ms nếu gọi mỗi 10ms)
            {
                buttons[i].count++;
                if (buttons[i].count == 3)
                {
                    // Đã qua thời gian chống dội ổn định, cập nhật trạng thái thực tế
                    if (buttons[i].stable_state == 1 && pin_val == 0)
                    {
                        // Phát hiện sườn xuống (Nhấn nút): chuyển từ Released (1) sang Pressed (0)
                        g_button_event = buttons[i].press_event;
                    }
                    buttons[i].stable_state = pin_val;
                }
            }
        }
        else
        {
            // Trạng thái thay đổi đột ngột (nhiễu hoặc mới bấm) -> reset bộ đếm dội
            buttons[i].raw_state = pin_val;
            buttons[i].count = 0;
        }
    }
}

Button_Event Button_GetEvent(void)
{
    Button_Event event = g_button_event;
    g_button_event = BTN_NONE; // Xóa sự kiện sau khi đọc
    return event;
}
