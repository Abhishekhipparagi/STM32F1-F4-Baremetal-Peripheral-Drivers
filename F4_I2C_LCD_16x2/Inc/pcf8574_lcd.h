/*
 * pcf8574_lcd.h — 16x2 LCD driver over I2C (PCF8574 backpack)
 *
 * PCF8574 byte: [D7 D6 D5 D4 | BL E RW RS]
 * LCD in 4-bit mode — each byte sent as two nibbles with E pulse
 *
 * Author: Abhishek Hipparagi
 */

#ifndef PCF8574_LCD_H
#define PCF8574_LCD_H

#include <stdint.h>

#define LCD_ADDR            0x27
#define LCD_BACKLIGHT       0x08
#define LCD_ENABLE          0x04
#define LCD_RW              0x02
#define LCD_RS              0x01

#define LCD_CMD_CLEAR        0x01
#define LCD_CMD_HOME         0x02
#define LCD_CMD_ENTRY_MODE   0x06
#define LCD_CMD_DISPLAY_ON   0x0C
#define LCD_CMD_FUNCTION_SET 0x28
#define LCD_CMD_LINE1        0x80
#define LCD_CMD_LINE2        0xC0

void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_SendCmd(uint8_t cmd);
void LCD_SendData(uint8_t data);
void LCD_SendString(char *str);

#endif /* PCF8574_LCD_H */
