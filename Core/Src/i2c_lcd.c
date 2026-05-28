#include "i2c_lcd.h"
#include "encoder.h"

static I2C_HandleTypeDef *g_hi2c = NULL;
static uint8_t g_lcd_addr = LCD_ADDR_1;
static uint8_t g_lcd_connected = 0;
static uint8_t g_backlight = 0x08; // P3 = Backlight LED control (0x08 is ON, 0x00 is OFF)

static void LCD_WriteNibble(uint8_t nibble, uint8_t rs)
{
    if (g_hi2c == NULL || !g_lcd_connected) return;

    uint8_t data = (nibble & 0xF0) | g_backlight | rs;
    uint8_t data_en_high = data | 0x04; // P2 = EN = 0x04
    uint8_t data_en_low = data;         // EN = 0

    HAL_I2C_Master_Transmit(g_hi2c, g_lcd_addr, &data_en_high, 1, 10);
    Delay_us(20); // Xung EN HIGH (chỉ cần >450ns, dùng 20us cho chắc chắn và cực kỳ nhanh)
    HAL_I2C_Master_Transmit(g_hi2c, g_lcd_addr, &data_en_low, 1, 10);
    Delay_us(20);
}

static void LCD_SendByte(uint8_t val, uint8_t rs)
{
    LCD_WriteNibble(val & 0xF0, rs);
    LCD_WriteNibble((val << 4) & 0xF0, rs);
}

void LCD_SendCommand(uint8_t cmd)
{
    LCD_SendByte(cmd, 0); // RS = 0
}

void LCD_SendData(uint8_t data)
{
    LCD_SendByte(data, 1); // RS = 1
}

HAL_StatusTypeDef LCD_Init(I2C_HandleTypeDef *hi2c)
{
    g_hi2c = hi2c;
    g_lcd_connected = 0;

    HAL_Delay(150); // Chờ màn hình khởi động nguồn ổn định (>100ms)

    // Quét toàn bộ dải địa chỉ I2C (từ 0x02 đến 0xFE, bước nhảy 2) để tự động phát hiện LCD
    for (uint16_t addr = 0x02; addr < 0xFF; addr += 1)
    {
        if (HAL_I2C_IsDeviceReady(g_hi2c, addr, 2, 10) == HAL_OK)
        {
            g_lcd_addr = addr;
            g_lcd_connected = 1;
            break; // Tìm thấy thiết bị đầu tiên phản hồi, dừng quét
        }
    }

    if (!g_lcd_connected)
    {
        return HAL_ERROR; // Trả về lỗi nếu không phát hiện thấy bất kỳ thiết bị I2C nào
    }

    // Khởi tạo ở chế độ 4-bit theo HD44780
    LCD_WriteNibble(0x30, 0);
    HAL_Delay(5);
    LCD_WriteNibble(0x30, 0);
    HAL_Delay(1);
    LCD_WriteNibble(0x30, 0);
    HAL_Delay(10);
    LCD_WriteNibble(0x20, 0); // Chuyển sang chế độ 4-bit
    HAL_Delay(10);

    // Cài đặt thông số hoạt động
    LCD_SendCommand(0x28); // Function set: 4-bit, 2 lines, 5x8 font
    HAL_Delay(1);
    LCD_SendCommand(0x0C); // Display control: Display ON, Cursor OFF, Blink OFF
    HAL_Delay(1);
    LCD_SendCommand(0x06); // Entry mode: Increment cursor, No shift
    HAL_Delay(1);
    LCD_SendCommand(0x01); // Clear display
    HAL_Delay(2);

    return HAL_OK;
}

void LCD_Clear(void)
{
    LCD_SendCommand(0x01);
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t address = 0x00;
    if (row == 0)
    {
        address = 0x80 + col;
    }
    else
    {
        address = 0xC0 + col;
    }
    LCD_SendCommand(address);
}

void LCD_SendString(char *str)
{
    while (*str)
    {
        LCD_SendData((uint8_t)(*str));
        str++;
    }
}
