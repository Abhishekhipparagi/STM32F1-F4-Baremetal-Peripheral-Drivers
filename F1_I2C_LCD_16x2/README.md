# F1_I2C_LCD_16x2

Bare-metal I2C + 16x2 LCD driver for STM32F103 (Blue Pill) with PCF8574 I2C backpack. Register-level, no HAL.

## What it does

Drives a 16x2 character LCD over I2C through a PCF8574 I/O expander. Displays "Hello World!" on line 1 and "STM32 + I2C LCD" on line 2.

```
STM32F103          PCF8574            16x2 LCD
┌──────────┐       ┌──────────┐       ┌──────────┐
│    PB6 ──┼─SCL──►│ SCL      │       │          │
│    PB7 ──┼─SDA──►│ SDA   P7─┼──D7──►│ DB7      │
│    3.3V  ├─VCC──►│ VCC   P6─┼──D6──►│ DB6      │
│    GND   ├─GND──►│ GND   P5─┼──D5──►│ DB5      │
│          │       │       P4─┼──D4──►│ DB4      │
│          │       │       P3─┼──BL──►│Backlight │
│          │       │       P2─┼──E ──►│ Enable   │
│          │       │       P1─┼──RW──►│ R/W      │
│          │       │       P0─┼──RS──►│ RS       │
└──────────┘       └──────────┘       └──────────┘
```

4.7k pull-ups needed on SCL/SDA — most PCF8574 LCD modules have them built in.

## Two drivers in this project

**I2C driver** (`stm32f1_i2c_driver`) — handles the I2C1 peripheral: START, STOP, address, data transfer. Standard mode 100 kHz.

**LCD driver** (`pcf8574_lcd`) — sits on top of the I2C driver. Handles HD44780 init sequence, 4-bit mode nibble splitting, Enable pulse generation, commands vs data (RS bit).

## Config

- I2C1 on APB1 (36 MHz), 100 kHz standard mode
- PB6 = SCL, PB7 = SDA (AF open-drain)
- PCF8574 address: 0x27 (7-bit)
- LCD: 4-bit mode, 2 lines, 5x8 font
- Delay: SysTick hardware timer (not crude loop)

## Files

```
Inc/
  stm32f1_i2c_driver.h
  pcf8574_lcd.h
  stm32f1_rcc_driver.h
Src/
  stm32f1_i2c_driver.c
  pcf8574_lcd.c
  stm32f1_rcc_driver.c
  main.c
```

## API

```c
// I2C
I2C1_Init();
I2C1_WriteByte(0x27, data);            // single byte transaction
I2C1_WriteMulti(0x27, buf, len);       // multi-byte, one START/STOP

// LCD
LCD_Init();
LCD_SetCursor(0, 0);                   // row 0, col 0
LCD_SendString("Hello!");
LCD_SendCmd(LCD_CMD_CLEAR);
LCD_SendData('A');                      // single character
```

## How 4-bit LCD over I2C works

PCF8574 has 8 output pins. Upper 4 (P7-P4) carry LCD data, lower 4 (P3-P0) carry control signals (backlight, enable, R/W, RS).

Each byte to the LCD is split into two nibbles. For each nibble, two I2C bytes are sent: one with E=1 (latch), one with E=0 (clock in). LCD reads data on the falling edge of E.

```
Sending 'H' (0x48):
  upper nibble 0x40: [0x4D] E=1  →  [0x49] E=0
  lower nibble 0x80: [0x8D] E=1  →  [0x89] E=0
```

RS=0 for commands, RS=1 for character data.

## I2C timing

```
CCR  = APB1_CLK / (2 * speed) = 36MHz / 200kHz = 180
TRISE = (APB1 in MHz) + 1 = 37
```

## LCD init sequence

HD44780 powers up in 8-bit mode (or unknown state). The init sequence forces a known state:

1. Send 0x30 three times (guarantees 8-bit mode regardless of initial state)
2. Send 0x20 (switch to 4-bit mode)
3. Then normal commands: function set, display off, clear, entry mode, display on

During steps 1-2, `LCD_SendNibble` is used (one E pulse). After 4-bit mode, `LCD_SendCmd` sends two nibbles per command.

## PCF8574 address

Default 0x27 when A0-A2 are all HIGH. If yours is different, change `LCD_ADDR` in `pcf8574_lcd.h`. Common addresses: PCF8574 = 0x20-0x27, PCF8574A = 0x38-0x3F.

## Build

STM32CubeIDE project. Needs F1 RCC driver files for 72 MHz clock.

## References

- RM0008 Reference Manual — I2C section
- HD44780 Datasheet — LCD controller
- PCF8574 Datasheet — I2C I/O expander