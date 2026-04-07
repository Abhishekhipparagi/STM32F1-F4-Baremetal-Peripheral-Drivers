/*
 * pcf8574_lcd.c — 16x2 LCD over I2C (PCF8574), bare metal
 *
 *
 * Author: Abhishek Hipparagi
 */

#include "pcf8574_lcd.h"
#include "stm32f4_i2c_driver.h"

extern void delay_ms(uint32_t ms);

// single nibble — only during init (LCD still in 8-bit mode)
static void lcd_send_nibble(uint8_t nibble)
{
    uint8_t data_t[2];
    nibble &= 0xF0;

    data_t[0] = nibble | LCD_BACKLIGHT | LCD_ENABLE;
    data_t[1] = nibble | LCD_BACKLIGHT;

    I2C1_WriteMulti(LCD_ADDR, data_t, 2);
}

// command (RS=0) — upper + lower nibble
void LCD_SendCmd(uint8_t cmd)
{
    uint8_t upper = cmd & 0xF0;
    uint8_t lower = (cmd << 4) & 0xF0;
    uint8_t data_t[4];

    data_t[0] = upper | LCD_BACKLIGHT | LCD_ENABLE;
    data_t[1] = upper | LCD_BACKLIGHT;
    data_t[2] = lower | LCD_BACKLIGHT | LCD_ENABLE;
    data_t[3] = lower | LCD_BACKLIGHT;

    I2C1_WriteMulti(LCD_ADDR, data_t, 4);
}

// data/character (RS=1)
void LCD_SendData(uint8_t data)
{
    uint8_t upper = data & 0xF0;
    uint8_t lower = (data << 4) & 0xF0;
    uint8_t data_t[4];

    data_t[0] = upper | LCD_BACKLIGHT | LCD_ENABLE | LCD_RS;
    data_t[1] = upper | LCD_BACKLIGHT | LCD_RS;
    data_t[2] = lower | LCD_BACKLIGHT | LCD_ENABLE | LCD_RS;
    data_t[3] = lower | LCD_BACKLIGHT | LCD_RS;

    I2C1_WriteMulti(LCD_ADDR, data_t, 4);
}

// HD44780 init: force 8-bit -> switch to 4-bit -> configure
void LCD_Init(void)
{
    delay_ms(50);

    lcd_send_nibble(0x30);
    delay_ms(5);
    lcd_send_nibble(0x30);
    delay_ms(1);
    lcd_send_nibble(0x30);
    delay_ms(10);

    lcd_send_nibble(0x20);  // 4-bit mode
    delay_ms(10);

    LCD_SendCmd(LCD_CMD_FUNCTION_SET);
    delay_ms(1);
    LCD_SendCmd(0x08);      // display off
    delay_ms(1);
    LCD_SendCmd(LCD_CMD_CLEAR);
    delay_ms(2);
    LCD_SendCmd(LCD_CMD_ENTRY_MODE);
    delay_ms(1);
    LCD_SendCmd(LCD_CMD_DISPLAY_ON);
    delay_ms(1);
}

void LCD_SendString(char *str)
{
    while (*str)
        LCD_SendData(*str++);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? LCD_CMD_LINE1 + col : LCD_CMD_LINE2 + col;
    LCD_SendCmd(addr);
}

void LCD_Clear(void)
{
    LCD_SendCmd(LCD_CMD_CLEAR);
    delay_ms(2);
}
