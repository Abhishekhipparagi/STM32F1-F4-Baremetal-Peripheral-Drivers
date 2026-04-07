/*
 * pcf8574_lcd.c — 16x2 LCD driver over I2C (PCF8574 backpack)
 *
 * LCD is in 4-bit mode. Each command/char byte is sent as
 * two nibbles, each with an Enable pulse through the PCF8574.
 *
 * Init sequence follows HD44780 datasheet:
 *   - send 0x30 three times (force 8-bit mode from any state)
 *   - send 0x20 (switch to 4-bit)
 *   - then normal commands via LCD_SendCmd
 *
 * During init (8-bit phase), use LCD_SendNibble (one E pulse).
 * After 4-bit mode is active, use LCD_SendCmd (two E pulses).
 *
 * Author: Abhishek Hipparagi
 */

#include "pcf8574_lcd.h"
#include "stm32f1_i2c_driver.h"

// forward declare — defined below main.c or wherever delay lives
extern void delay_ms(uint32_t ms);

// single nibble — only used during init when LCD is in 8-bit mode
static void lcd_send_nibble(uint8_t nibble)
{
    uint8_t data_t[2];
    nibble &= 0xF0;

    data_t[0] = nibble | LCD_BACKLIGHT | LCD_ENABLE;    // E=1
    data_t[1] = nibble | LCD_BACKLIGHT;                  // E=0

    I2C1_WriteMulti(LCD_ADDR, data_t, 2);
}

// command byte (RS=0) — sends upper then lower nibble
void LCD_SendCmd(uint8_t cmd)
{
    uint8_t upper = cmd & 0xF0;
    uint8_t lower = (cmd << 4) & 0xF0;
    uint8_t data_t[4];

    data_t[0] = upper | LCD_BACKLIGHT | LCD_ENABLE;     // upper, E=1
    data_t[1] = upper | LCD_BACKLIGHT;                   // upper, E=0
    data_t[2] = lower | LCD_BACKLIGHT | LCD_ENABLE;     // lower, E=1
    data_t[3] = lower | LCD_BACKLIGHT;                   // lower, E=0

    I2C1_WriteMulti(LCD_ADDR, data_t, 4);
}

// data byte (RS=1) — character to display
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

// HD44780 power-on init sequence
// sends 0x30 three times to force 8-bit mode from unknown state,
// then 0x20 to switch to 4-bit mode
void LCD_Init(void)
{
    delay_ms(50);           // wait for LCD power-on (>40ms)

    // 8-bit phase — LCD reads one nibble per E pulse
    lcd_send_nibble(0x30);
    delay_ms(5);
    lcd_send_nibble(0x30);
    delay_ms(1);
    lcd_send_nibble(0x30);
    delay_ms(10);

    lcd_send_nibble(0x20);  // switch to 4-bit mode
    delay_ms(10);

    // 4-bit phase — now LCD expects two nibbles per command
    LCD_SendCmd(LCD_CMD_FUNCTION_SET);  // 0x28: 4-bit, 2 lines, 5x8
    delay_ms(1);
    LCD_SendCmd(0x08);                  // display OFF
    delay_ms(1);
    LCD_SendCmd(LCD_CMD_CLEAR);         // clear
    delay_ms(2);
    LCD_SendCmd(LCD_CMD_ENTRY_MODE);    // 0x06: increment cursor
    delay_ms(1);
    LCD_SendCmd(LCD_CMD_DISPLAY_ON);    // 0x0C: display ON, cursor OFF
    delay_ms(1);
}

void LCD_SendString(char *str)
{
    while (*str)
        LCD_SendData(*str++);
}

// row = 0 or 1, col = 0-15
// DDRAM: line 1 starts at 0x80, line 2 at 0xC0
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
