#ifndef __I2C_LCD_H
#define __I2C_LCD_H

#include "stm32f1xx_hal.h"

// Địa chỉ I2C mặc định của LCD PCF8574 (0x27 << 1 = 0x4E hoặc 0x3F << 1 = 0x7E)
#define LCD_ADDR_1 0x4E
#define LCD_ADDR_2 0x7E

// Khởi tạo LCD
// hi2c: Trỏ tới cấu hình I2C1
// Trả về HAL_OK nếu khởi tạo thành công
HAL_StatusTypeDef LCD_Init(I2C_HandleTypeDef *hi2c);

// Gửi lệnh điều khiển LCD
void LCD_SendCommand(uint8_t cmd);

// Gửi ký tự hiển thị lên LCD
void LCD_SendData(uint8_t data);

// Xóa màn hình
void LCD_Clear(void);

// Di chuyển con trỏ đến vị trí hàng, cột (hàng: 0-1, cột: 0-15)
void LCD_SetCursor(uint8_t row, uint8_t col);

// Gửi một chuỗi ký tự hiển thị
void LCD_SendString(char *str);

#endif /* __I2C_LCD_H */
