# F4 SPI1 + W25Q32 Flash — Bare Metal

SPI1 master driver for STM32F446RE Nucleo + Winbond W25Q32 NOR flash device driver. Two-layer design: a generic SPI peripheral driver underneath, a flash device driver on top. No HAL.

## Hardware

- **Board:** Nucleo-F446RE
- **MCU:** STM32F446RE, Cortex-M4 @ 180 MHz
- **Flash:** Winbond W25Q32 (32 Mbit / 4 MB SPI NOR)
- **SPI:** SPI1, mode 0, **22.5 MHz** (90 MHz APB2 / 4)

| Nucleo pin | Arduino header |   Signal   | W25Q32 pin |
|------------|----------------|------------|------------|
| PA4        |     A2 (CN8-3) |CS (GPIO)   |        /CS |
| PA5        |    D13 (CN5-6) | SCK (AF5)  |        CLK |
| PA6        |    D12 (CN5-5) | MISO (AF5) |         DO |
| PA7        |    D11 (CN5-4) | MOSI (AF5) |         DI |
| 3V3        |      CN6-4     | —          |VCC, /WP, /HOLD |
| GND        |      CN6-6     | —          |        GND |
| **PC0** | A5 (CN8-6) | status LED (external) | — |

`/WP` and `/HOLD` must be tied HIGH or the chip ignores writes / stays held.

## ⚠ PA5 conflict on Nucleo

On Nucleo-F446RE the onboard **LD2 LED is hard-wired to PA5**, which is also **SPI1_SCK**. They can't share — when SPI is active, LD2 will flicker with the clock signal and you can't use it as a status LED. This project uses an **external LED on PC0** instead.

If you need the onboard LED, the alternative is to remap SPI1 to **PB3 (SCK) / PB4 (MISO) / PB5 (MOSI)**, all on AF5.

## Architecture

```
+------------------+
|     main.c       |   test app
+--------+---------+
         |
+--------v---------+   +-----------------+
|  w25q32_driver   |-->| stm32f4_spi_drv |   peripheral driver
+------------------+   +-----------------+
                              |
                       +------v------+
                       |   SPI1 HW   |
                       +-------------+
```

The SPI driver knows nothing about flash chips — it just does master-mode byte transfers. The W25Q driver knows nothing about STM32 registers (other than the CS GPIO) — it just sends opcodes through `SPI1_TransmitReceive`. Swap the chip out and only the device layer changes.

## Files

```
Inc/
  stm32f4_rcc_driver.h
  stm32f4_uart_driver.h
  stm32f4_spi_driver.h
  w25q32_driver.h
Src/
  stm32f4_rcc_driver.c
  stm32f4_uart_driver.c
  stm32f4_spi_driver.c
  w25q32_driver.c
  main.c
```

Pulls in the RCC driver from `F4_RCC_Clock_180MHz` and the UART driver from `F4_UART2_TX_RX` — copy those `.h/.c` pairs into this project's `Inc/` and `Src/` folders.

## SPI driver API

```c
typedef enum {
    SPI_BR_DIV2, SPI_BR_DIV4, SPI_BR_DIV8, SPI_BR_DIV16,
    SPI_BR_DIV32, SPI_BR_DIV64, SPI_BR_DIV128, SPI_BR_DIV256
} SPI_BaudDiv_t;

void    SPI1_Init(SPI_BaudDiv_t baud_div);
uint8_t SPI1_TransmitReceive(uint8_t data);
```

Mode 0, 8-bit, MSB first, software NSS. Pins are fixed at PA5/PA6/PA7 with AF5. CS is owned by the device driver.

## W25Q32 driver API

```c
void     W25Q_Init(void);                                              // CS pin
void     W25Q_Reset(void);
uint32_t W25Q_ReadJEDECID(void);                                       // expect 0xEF4016

void     W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len);  // <=256
void     W25Q_SectorErase(uint32_t addr);                              // 4 KB
void     W25Q_ChipErase(void);                                         // 4 MB
```

## Usage

```c
RCC_SystemClock_Config_180MHz();
SPI1_Init(SPI_BR_DIV4);
W25Q_Init();

uint8_t data[] = "hello flash";
W25Q_SectorErase(0);
W25Q_PageProgram(0, data, sizeof(data));

uint8_t back[32] = {0};
W25Q_ReadData(0, back, sizeof(data));
```

## F1 vs F4 — what actually changes for SPI

| | F103 (Blue Pill) | F446 (Nucleo) |
|---|---|---|
| APB2 clock | 72 MHz | 90 MHz |
| SPI ÷4 speed | 18 MHz | 22.5 MHz |
| GPIO regs | CRL / CRH | MODER / OTYPER / OSPEEDR / PUPDR / AFR |
| GPIO bus | APB2 | AHB1 |
| AF mux | fixed (or AFIO remap) | explicit AF5 in AFR |
| CS macro | `GPIO_ODR_ODR4` | `GPIO_ODR_OD4` |
| LED conflict | none (PC13) | **PA5 = LD2 = SCK!** |
| SPI CR1/CR2/SR/DR | — | identical |

The SPI peripheral itself is the same. Only the GPIO wiring around it differs. The W25Q command sequences, the BRR-style CR1 setup, the TXE/RXNE polling loop — all copy-paste from the F1 version.

## How it works

**SPI transfer.** Full duplex — every TX is also an RX. To write, send the byte and ignore what comes back. To read, send a dummy `0xFF` and use the return value. The clock only runs during transmission, so you have to clock out *something* to clock data in.

**CS handling.** Manual GPIO on PA4 (F4 syntax: `MODER=01`, `OTYPER=0`, `OSPEEDR=11`). Hardware NSS would toggle CS between every byte, which breaks W25Q commands — the chip needs CS held LOW for the entire `opcode + address + data` sequence.

**Write flow.** Flash bits can only change `1 -> 0`. To overwrite a byte you must first erase it (sets every byte in a 4 KB sector to `0xFF`), then page-program over it. Forgetting to erase is the #1 cause of "my data is corrupted" bugs. Every write also requires Write Enable (`0x06`) immediately before, because the chip auto-clears the WEL bit after each program/erase.

**Busy polling.** After a program or erase, the chip is internally busy and ignores everything except Read Status (`0x05`). The driver spins on bit 0 of status reg 1 until it clears.

## Gotchas

- **Forgetting AFR** — pin stays at AF0, SPI1 never reaches the pins. Hardest bug to spot because the GPIO is configured "correctly" otherwise.
- **`SSI=1` with `SSM=1`** — without SSI, the peripheral sees its own NSS as LOW, thinks another master grabbed the bus, and silently switches itself to slave mode. Set both bits.
- **Crossing a 256-byte page boundary in PageProgram** — the address wraps inside the page instead of advancing. Caller must split the buffer.
- **Writing without erasing first** — `0`s stick. Reads come back garbled.
- **PA5 conflict** — see the warning above. Don't use the onboard LED.

## Test output

Plug in USB, open serial at 9600 8N1 on the ST-Link VCP port:

```
F446RE SPI + W25Q32 test
Reset done
JEDEC ID: 0x00EF4016
W25Q32 detected
Erasing sector 0...
Erase done
Writing: Hello from F446RE SPI!
Write done
Read back: Hello from F446RE SPI!
VERIFY: PASS
```

External LED on PC0 starts blinking after the test completes.

## Build

STM32CubeIDE bare-metal project for STM32F446RETx. No HAL. Drop the four driver pairs + `main.c` into `Inc/` and `Src/`. Don't forget to also pull in the RCC and UART drivers from the previous F4 projects.