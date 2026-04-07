# F1 SPI1 + W25Q32 Flash — Bare Metal

SPI1 master driver for STM32F103 + Winbond W25Q32 NOR flash device driver. Two-layer design: a generic SPI peripheral driver underneath, a flash device driver on top. No HAL.

## Hardware

- **Board:** Blue Pill (STM32F103C8T6 / C6T6)
- **MCU:** Cortex-M3 @ 72 MHz
- **Flash:** Winbond W25Q32 (32 Mbit / 4 MB SPI NOR)
- **SPI:** SPI1, mode 0, 18 MHz (72 / 4)

| STM32 pin | Signal | W25Q32 pin |
|---|---|---|
| PA5 | SCK  | CLK |
| PA7 | MOSI | DI |
| PA6 | MISO | DO |
| PA4 | CS (GPIO) | /CS |
| 3V3 | — | VCC, /WP, /HOLD |
| GND | — | GND |

`/WP` and `/HOLD` must be tied HIGH or the chip ignores writes / stays held.

## Architecture

```
+------------------+
|     main.c       |   test app
+--------+---------+
         |
+--------v---------+   +-----------------+
|  w25q32_driver   |-->| stm32f1_spi_drv |   peripheral driver
+------------------+   +-----------------+
                              |
                       +------v------+
                       |   SPI1 HW   |
                       +-------------+
```

The SPI driver knows nothing about flash chips — it just does master-mode byte transfers. The W25Q driver knows nothing about STM32 registers (other than the CS GPIO) — it just sends opcodes and addresses through `SPI1_TransmitReceive`. Swap the chip out for a SD card or sensor and only the device layer changes.

## Files

```
Inc/
  stm32f1_rcc_driver.h
  stm32f1_uart_driver.h
  stm32f1_spi_driver.h
  w25q32_driver.h
Src/
  stm32f1_rcc_driver.c
  stm32f1_uart_driver.c
  stm32f1_spi_driver.c
  w25q32_driver.c
  main.c
```

Pulls in the RCC driver from `F1_RCC_Clock_Config` and the UART driver from `F1_UART2_TX_RX` — copy those `.h/.c` pairs into this project's `Inc/` and `Src/` folders.

## SPI driver API

```c
typedef enum {
    SPI_BR_DIV2, SPI_BR_DIV4, SPI_BR_DIV8, SPI_BR_DIV16,
    SPI_BR_DIV32, SPI_BR_DIV64, SPI_BR_DIV128, SPI_BR_DIV256
} SPI_BaudDiv_t;

void    SPI1_Init(SPI_BaudDiv_t baud_div);
uint8_t SPI1_TransmitReceive(uint8_t data);
```

Mode 0, 8-bit, MSB first, software NSS. Pins are fixed at PA5/PA6/PA7. CS is owned by the device driver, not the SPI driver.

## W25Q32 driver API

```c
void     W25Q_Init(void);                                              // CS pin
void     W25Q_Reset(void);
uint32_t W25Q_ReadJEDECID(void);                                       // expect 0xEF4016

void     W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);     // any length
void     W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len);  // <=256
void     W25Q_SectorErase(uint32_t addr);                              // 4 KB
void     W25Q_ChipErase(void);                                         // 4 MB
```

## Usage

```c
RCC_SystemClock_Config_72MHz();
SPI1_Init(SPI_BR_DIV4);
W25Q_Init();

uint8_t data[] = "hello flash";
W25Q_SectorErase(0);
W25Q_PageProgram(0, data, sizeof(data));

uint8_t back[32] = {0};
W25Q_ReadData(0, back, sizeof(data));
```

## How it works

**SPI transfer.** Full duplex — every TX is also an RX. To write, send the byte and ignore what comes back. To read, send a dummy `0xFF` and use the return value. The clock only runs during transmission, so you have to clock out *something* to clock data in.

**CS handling.** Manual GPIO on PA4. Hardware NSS would toggle CS between every byte, which breaks W25Q commands — the chip needs CS held LOW for the entire `opcode + address + data` sequence. `cs_low()` / `cs_high()` are static inlines in the device driver.

**Write flow.** Flash bits can only change `1 -> 0`. To overwrite a byte you must first erase it (sets every byte in a 4 KB sector to `0xFF`), then page-program over it. Forgetting to erase is the #1 cause of "my data is corrupted" bugs. Every write also requires a Write Enable (`0x06`) immediately before, because the chip auto-clears the WEL bit after each program/erase.

**Busy polling.** After a program or erase, the chip is internally busy and ignores everything except Read Status (`0x05`). The driver spins on bit 0 of status reg 1 until it clears.

## Why push-pull and not open-drain?

| Bus | GPIO type | Why |
|---|---|---|
| UART TX | push-pull | point-to-point, one driver |
| **SPI SCK/MOSI** | **push-pull** | **point-to-point, CS selects one slave** |
| I2C SDA/SCL | open-drain | shared bus, multiple drivers |

SPI is fast and clean *because* it doesn't share — that's also why CS is required.

## SPI1 vs SPI2 on F103

SPI1 is on **APB2 (72 MHz)** — max SPI clock is 36 MHz at the `/2` prescaler.
SPI2 is on **APB1 (36 MHz)** — max SPI clock is 18 MHz at `/2`.

If you ever port this to SPI2, change the clock enable register and the prescaler math.

## Gotchas

- **Forgetting `SSI=1` with `SSM=1`** — the peripheral sees its own NSS pin as LOW, thinks another master grabbed the bus, and silently switches itself to slave mode. Nothing on the wire. Set both bits.
- **Crossing a 256-byte page boundary in PageProgram** — the address wraps inside the page instead of advancing. Caller must split the buffer.
- **Writing without erasing first** — `0`s stick. Reads come back garbled.
- **Not waiting for BUSY** — sending the next command while a write is still in progress just gets ignored.

## Test output

Open serial at 9600 8N1 on PA2:

```
SPI + W25Q32 test
Reset done
JEDEC ID: 0x00EF4016
W25Q32 detected
Erasing sector 0...
Erase done
Writing: Hello from SPI!
Write done
Read back: Hello from SPI!
VERIFY: PASS
```

LED on PC13 starts blinking after the test completes.

## Build

STM32CubeIDE bare-metal project for STM32F103C6Tx (or C8Tx). No HAL. Drop the four driver pairs + `main.c` into `Inc/` and `Src/`. Don't forget to also pull in the RCC and UART drivers from the previous projects.