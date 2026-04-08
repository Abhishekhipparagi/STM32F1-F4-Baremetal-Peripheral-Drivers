# F4 I²C + PCF8574 + 16×2 LCD — Bare Metal

I²C1 master driver for STM32F446RE Nucleo plus a PCF8574 "I²C backpack" LCD driver on top. Two-layer design: generic I²C peripheral driver underneath, character-LCD device driver on top. No HAL.

## Hardware

- **Board:** Nucleo-F446RE
- **MCU:** STM32F446RE, Cortex-M4 @ 180 MHz, APB1 @ 45 MHz (I²C1 lives here)
- **I²C:** I²C1, 100 kHz Standard Mode
- **Pins:** PB8 = SCL (AF4), PB9 = SDA (AF4)
- **LCD:** Generic 16×2 HD44780 with a PCF8574 I²C backpack
- **PCF8574 address:** `0x27` 7-bit (try `0x3F` if yours is the PCF8574A variant)
- **LED:** PA5 (LD2, active HIGH)

```
+----------+         +----------+          +----------+
| Nucleo   |         | PCF8574  |          | 16x2 LCD |
|  PB8  ---+---SCL-->| SCL      |          |          |
|  PB9  ---+---SDA-->| SDA      |          |          |
|   5V  ---+---VCC-->| VCC   P7-+---D7 --->| DB7      |
|  GND  ---+---GND-->| GND   P6-+---D6 --->| DB6      |
|          |         |       P5-+---D5 --->| DB5      |
|          |         |       P4-+---D4 --->| DB4      |
|          |         |       P3-+---BL --->| Backlight|
|          |         |       P2-+---E  --->| Enable   |
|          |         |       P1-+---RW --->| R/W      |
|          |         |       P0-+---RS --->| RS       |
+----------+         +----------+          +----------+
```

The LCD runs in **4-bit mode** through the expander. The PCF8574 gives us 8 GPIO pins over I²C — we wire 4 of them to the LCD's data lines (DB4-DB7) and the other 4 to the control signals (RS, RW, E) plus the backlight.

Most PCF8574 modules already have 4.7 kΩ pull-ups on SCL/SDA, but the driver also enables internal pull-ups as a safety net.

## Architecture

```
+------------------+
|     main.c       |   test app
+--------+---------+
         |
+--------v---------+   +---------------------+
|   pcf8574_lcd    |-->| stm32f4_i2c_driver  |   peripheral driver
+------------------+   +---------------------+
                                 |
                          +------v------+
                          |   I2C1 HW   |
                          +-------------+
```

The I²C driver knows nothing about LCDs — it just does I²C master writes. The LCD driver knows nothing about STM32 registers — it formats 4-bit nibbles into PCF8574 output bytes and calls `I2C1_WriteMulti`. Swap the LCD out for a different PCF8574-based device and only the top layer changes; swap to a different MCU and only the bottom layer changes.

## Files

```
Inc/
  stm32f4_rcc_driver.h
  stm32f4_i2c_driver.h
  pcf8574_lcd.h
Src/
  stm32f4_rcc_driver.c
  stm32f4_i2c_driver.c
  pcf8574_lcd.c
  main.c
```

Pulls in the RCC driver from `F4_RCC_Clock_180MHz`.

## I²C timing (100 kHz)

F446 I²C is the same "I²C v1" peripheral as the F103 — standard mode only needs three registers: `CR2.FREQ`, `CCR`, `TRISE`.

```
APB1 = 45 MHz

CR2.FREQ = 45                                     (APB1 in MHz)
CCR      = 45,000,000 / (2 * 100,000) = 225       (half period)
TRISE    = 45 + 1 = 46                            (max rise time)

SCL period = 2 * 225 * (1/45 MHz) = 10 us  ->  100 kHz  OK
```

For Fast Mode (400 kHz) you'd change `CCR` to 38 (with the Fm bit set and the duty bit configured) and recompute `TRISE` from the 300 ns rise-time spec. This driver sticks to Standard Mode because the LCD and PCF8574 are happy there and it's one less variable.

## PCF8574 byte layout

The PCF8574 is an 8-bit quasi-bidirectional GPIO expander. One I²C write latches 8 bits onto its output pins. We wire those 8 pins to the LCD like this:

```
bit  7    6    5    4   |  3    2    1    0
    D7   D6   D5   D4   |  BL   E    RW   RS
    +-- LCD data --+    |  |    |    |    +--> LCD RS  (0=cmd, 1=data)
                        |  |    |    +-------> LCD RW  (tied 0 = write)
                        |  |    +------------> LCD E   (enable strobe)
                        |  +-----------------> backlight transistor (1=on)
                        +--------------------> (not used)
```

Sending a byte to the LCD over 4-bit mode requires **two nibble transfers**, and each nibble has to be toggled with the E signal high-then-low. So one "LCD byte" becomes four PCF8574 writes:

```
1. upper nibble | BL | E=1 | RS   (present data, pulse E high)
2. upper nibble | BL | E=0 | RS   (latch on falling edge)
3. lower nibble | BL | E=1 | RS   (present data, pulse E high)
4. lower nibble | BL | E=0 | RS   (latch on falling edge)
```

The driver builds that 4-byte sequence once and ships it with a single `I2C1_WriteMulti()` call, so it's one start-address-data...-stop transaction per LCD character instead of four separate ones.

## Driver APIs

**I²C peripheral driver**

```c
void I2C1_Init(void);

void I2C1_Start(void);
void I2C1_Stop(void);
void I2C1_WriteAddr(uint8_t addr);
void I2C1_WriteData(uint8_t data);

void I2C1_WriteByte(uint8_t saddr, uint8_t data);
void I2C1_WriteMulti(uint8_t saddr, uint8_t *data, uint8_t len);
```

Blocking master mode, 7-bit addressing. Reads aren't implemented because the LCD doesn't need them — this is a write-only demo.

**PCF8574 LCD driver**

```c
void LCD_Init(void);

void LCD_SendCmd(uint8_t cmd);
void LCD_SendData(uint8_t data);
void LCD_SendString(char *str);

void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Clear(void);
```

`LCD_Init()` handles the HD44780 wakeup dance (three `0x30` commands, then the `0x20` switch to 4-bit) plus function set, display on, entry mode, and clear.

## Usage

```c
RCC_SystemClock_Config_180MHz();
I2C1_Init();
LCD_Init();

LCD_SetCursor(0, 0);
LCD_SendString("Hello World!");

LCD_SetCursor(1, 0);
LCD_SendString("STM32F446 + I2C");
```

## How it works

**I²C transaction.** A single-byte write goes: `START` → slave address + W → wait for `ADDR` (address ACKed), clear it by reading `SR1` then `SR2` → write data byte → wait for `BTF` (byte transfer finished) → `STOP`. The driver exposes each step (`I2C1_Start`, `I2C1_WriteAddr`, `I2C1_WriteData`, `I2C1_Stop`) for clarity and also provides `WriteByte` / `WriteMulti` wrappers that batch the whole sequence.

**LCD init dance.** HD44780 controllers come up in 8-bit mode and don't know their data width until you tell them. The init sequence is:

```
power on, wait >40 ms
send 0x30 (8-bit phase)
wait >4 ms
send 0x30
wait >100 us
send 0x30
wait
send 0x20           <- NOW in 4-bit mode
function set 0x28   <- 4-bit, 2 lines, 5x8 font
display off 0x08
clear 0x01
entry mode 0x06     <- auto-increment cursor, no shift
display on 0x0C
```

The first three `0x30` bytes are sent as **single nibbles** (via `LCD_SendNibble`), because the LCD is still in 8-bit mode and interpreting one PCF8574 write as one full command. After the switch to 4-bit, every command and character is sent as **two nibbles** (via `LCD_SendCmd` / `LCD_SendData`).

**Keeping the backlight on.** Every single PCF8574 write has to include the backlight bit, because the expander latches all 8 output pins on each write. Drop the backlight bit in any command and the screen goes dark until the next write. The driver ORs `LCD_BACKLIGHT` into every byte it constructs.

**E strobe.** The LCD latches data on the falling edge of the E pin. So for every nibble we send the data twice — once with `E=1`, once with `E=0`. The tiny I²C-induced delay between those two writes is enough for the LCD to see the pulse, which is why this works without explicit microsecond delays.

## GPIO config (F4 style)

F4 splits GPIO config across 5 separate registers (vs F1's packed CRL/CRH):

- **`MODER`** = `10` → alternate function mode
- **`OTYPER`** = `1` → **open-drain** ← this is the I²C-critical bit. Forget it and I²C silently fails.
- **`OSPEEDR`** = `11` → very high speed (overkill for 100 kHz, safe for 400 kHz later)
- **`PUPDR`** = `01` → internal pull-up (backup for the module's external 4.7 kΩ)
- **`AFR`** = `4` → AF4 = I²C1

Open-drain matters because I²C is a **shared bus** — any slave is allowed to pull SCL/SDA low at any time (clock stretching, ACK, etc.). If the master drove the lines push-pull, two devices pulling opposite directions would short the bus. Open-drain + pull-up resistors means every device can only *release* (let the pull-up win) or *pull low* (drive to GND) — no conflicts possible.

## F1 vs F4 — what changes for I²C

| | F103 | F446 |
|---|---|---|
| APB1 clock | 36 MHz | 45 MHz |
| `CR2.FREQ` | 36 | 45 |
| `CCR` (100 kHz) | 180 | 225 |
| `TRISE` | 37 | 46 |
| GPIO regs | CRL/CRH | MODER/OTYPER/OSPEEDR/PUPDR/AFR |
| GPIO bus | APB2 | AHB1 |
| Open-drain | `CNF = 11` in CRL | `OTYPER = 1` (separate register) |
| AF mux | AFIO remap bit | explicit AF4 in AFR |
| CR1/CR2/SR1/SR2/DR | — | identical |

The I²C peripheral itself is the same silicon. The status flags (`SB`, `ADDR`, `TXE`, `BTF`, `BUSY`), control bits (`PE`, `START`, `STOP`, `ACK`, `SWRST`), and register names are byte-identical between F103 and F446. Only the GPIO setup, the clock feeding the peripheral, and the derived `CCR`/`TRISE` values change.

## Gotchas

- **Forgetting `OTYPER = 1`** — pins default to push-pull, I²C bus shorts on every ACK, nothing works, no obvious error. Number one "why isn't my I²C initializing" bug on F4.
- **Forgetting `AFR = 4`** — pins stay at AF0, I²C1 never reaches them, SCL/SDA look stuck. Ctrl+click the AF macro in your IDE if you're not sure; on F446 I²C1/2/3 are all on AF4.
- **Wrong PCF8574 address** — `0x27` is the default for the plain PCF8574, `0x3F` is the PCF8574A. If the LCD stays blank, try the other one (or scan the bus).
- **Missing the `SR1`+`SR2` read after `ADDR`** — the flag stays set, the driver hangs waiting for `TXE`. The driver does the read explicitly, don't optimize it out.
- **Dropping the backlight bit** — any PCF8574 write without the BL bit turns the backlight off until the next write.
- **Trying to drive the LCD in 8-bit mode** — the PCF8574 only gives you 8 GPIO pins total, and 4 of them are needed for RS/RW/E/BL. You're stuck with 4-bit mode. That's fine, the LCD controller natively supports it.
- **PCF8574 pulled to 5 V without level shifting** — the PCF8574 and the LCD are both 5 V parts, which is why we power them from the Nucleo's 5 V pin. The I²C lines themselves are still 3.3 V from the STM32 side, but the PCF8574 tolerates that fine because of the open-drain topology.

## Test

1. Wire the LCD backpack per the diagram above
2. Flash and reset the board
3. The LCD should show:
   ```
   Hello World!
   STM32F446 + I2C
   ```
4. LD2 on PA5 toggles at 1 Hz as a "main loop is running" indicator
5. Nothing on the display? Walk through the gotchas list top to bottom — `OTYPER`, `AFR`, address, backlight bit. That's 90% of first-try I²C debugging.

## Build

STM32CubeIDE bare-metal project for STM32F446RETx. No HAL. Drop the three driver pairs + `main.c` into `Inc/` and `Src/`. Pull in the RCC driver from `F4_RCC_Clock_180MHz`. Delete the auto-generated `main.c` that CubeIDE creates before dropping mine in — duplicate symbols will break the link.