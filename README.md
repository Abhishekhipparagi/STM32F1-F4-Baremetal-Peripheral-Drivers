# STM32 Bare-Metal Peripheral Drivers — F1 & F4

![Language](https://img.shields.io/badge/Language-C-blue.svg)
![Architecture](https://img.shields.io/badge/Arch-ARM%20Cortex--M3%20%7C%20M4F-informational)
![MCU](https://img.shields.io/badge/MCU-STM32F103%20%7C%20STM32F446RE-success)
![HAL](https://img.shields.io/badge/HAL-None-critical)
![Toolchain](https://img.shields.io/badge/Toolchain-STM32CubeIDE-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green.svg)

Ten peripheral drivers I wrote from scratch for the STM32F1 and STM32F4 families. No HAL, no LL, no CubeMX-generated init code. Just the reference manual, the datasheet, and a lot of `volatile uint32_t *`.

I built this repo because I wanted to actually understand what `HAL_Init()` was hiding from me. So I picked five common peripherals — RCC, UART, SPI, I²C, CAN — and wrote each one twice: once for a Blue Pill (Cortex-M3), and once for a Nucleo-F446RE (Cortex-M4F). Same demos, two different chips, all the way down to the bit level.

---

## 🎯 What This Repo Shows

If you're looking at this for a hiring decision, here's the short version of what's actually in here:

| Skill | Where to look |
|---|---|
| Register-level C — `volatile`, bit-masking, memory-mapped structs | every `*_driver.c` file |
| Reading reference manuals and datasheets | RCC clock-tree config, CAN bit-timing math, W25Q32 command set |
| Clock tree bring-up from HSI/HSE through PLL | `F1_RCC_Clock_Config`, `F4_RCC_Clock_180MHz` |
| Serial protocols, both polled and interrupt-driven | UART, SPI, I²C, CAN drivers |
| Talking to external ICs | W25Q32 NOR flash, PCF8574 I²C LCD backpack, CAN transceivers |
| Interrupts and NVIC setup | CAN RX FIFO0 IRQ with a user callback |
| Verifying bring-up on the bench | MCO output hooks so you can confirm PLL lock with a scope |
| Porting between MCU families | every driver exists in both an F1 and an F4 version |

---

## 🧩 The Projects

Each folder is a self-contained STM32CubeIDE project. Driver sources sit in `Src/` and `Inc/` next to a `main.c` that doubles as the demo and the test harness. The header comment at the top of every `main.c` lists the wiring, so you don't have to dig through the source to figure out which pin goes where.

### STM32F1 — Blue Pill (STM32F103C8) @ 72 MHz

| Project | Peripheral | What it does |
|---|---|---|
| `F1_RCC_Clock_Config` | **RCC** | HSE → PLL → 72 MHz SYSCLK, with APB1/APB2/AHB prescalers, flash latency, and an MCO output on PA8 so you can verify the clock with a scope |
| `F1_UART2_TX_RX` | **USART2** | 9600 8N1 echo on PA2/PA3, toggles the PC13 LED on every byte received |
| `F1_SPI_W25Q32_Flash` | **SPI1 + W25Q32** | Reads the JEDEC ID (`0xEF4016`), erases a sector, page-programs a string, reads it back, and runs a `memcmp` to print PASS or FAIL over UART |
| `F1_I2C_LCD_16x2` | **I²C1 + PCF8574** | 4-bit LCD driver going through an I²C GPIO expander backpack at address `0x27` |
| `F1_CAN1_TX_RX` | **bxCAN** | 500 kbps Normal mode, 87.5% sample point, hardware filter, IRQ-driven RX with a user callback, TX counter frame every 500 ms |

### STM32F4 — Nucleo-F446RE @ 180 MHz

| Project | Peripheral | What it does |
|---|---|---|
| `F4_RCC_Clock_180MHz` | **RCC** | HSE → PLL(M, N, P, Q) → **180 MHz** (the F446's max), with OverDrive enabled and 5 wait-state flash. MCO1 on PA8 |
| `F4_UART2_TX_RX` | **USART2** | 9600 baud echo over the ST-Link virtual COM port. Plug in the USB cable, open a terminal, that's it |
| `F4_SPI_W25Q32_Flash` | **SPI1 + W25Q32** | Same W25Q32 test sequence as the F1 version, ported to the F4 SPI peripheral at higher clock |
| `F4_I2C_LCD_16x2` | **I²C1 + PCF8574** | LCD driver on PB8/PB9, with a SysTick-based `delay_ms()` |
| `F4_CAN1_TX_RX` | **bxCAN** | 500 kbps on PB8/PB9 (the CN5 Arduino header), IRQ-driven RX, TX counter frame, and a fast-blink fault LED if init fails so you don't sit there wondering why nothing works |

---

## 🏗️ Why Two MCUs

The F1 (Cortex-M3) and F4 (Cortex-M4F) sound similar on paper, but the GPIO block, the clock tree, and the flash controller all work pretty differently once you're at the register level. Writing the same five drivers twice was the fastest way to actually internalise those differences instead of memorising them from a blog post.

A couple of examples of what changes when you port from F1 to F4:

- The GPIO config model is completely different. F1 packs the mode and config bits into `CRL`/`CRH`. F4 splits them across `MODER`, `OTYPER`, `OSPEEDR`, `PUPDR`, and `AFR`. Same pin, two totally different setup procedures.
- The clock tree on the F1 is basically a single PLL multiplier off HSE. On the F4 you have M/N/P/Q dividers plus OverDrive mode plus flash wait states to actually hit 180 MHz safely.

On the API side I tried to keep things small and obvious. No handle structs where a single `Init()` is enough, no callback layers I don't need. The CAN driver is the one place callbacks earn their keep, because once you're in IRQ context you don't really have a choice.

---

## 🔧 Toolchain & Hardware

**Build environment**
- STM32CubeIDE — every folder imports straight in as an existing project

**Flash and debug**
- ST-Link V2 for the Blue Pill, the onboard ST-Link for the Nucleo
- Either OpenOCD or the CubeIDE debugger works fine

**External hardware in the demos**
- W25Q32 SPI NOR flash breakout (3.3 V)
- 16×2 character LCD with a PCF8574 I²C backpack
- A CAN transceiver (MCP2551, SN65HVD230, or TJA1050) plus 120 Ω termination at both ends of the bus, plus at least one other node so frames actually get ACKed
- A USB–TTL adapter for the Blue Pill UART demo (the Nucleo doesn't need one — it has VCP built in)

---

## 🚀 Getting Started

```bash
git clone https://github.com/Abhishekhipparagi/STM32F1-F4-Baremetal-Peripheral-Drivers.git
```

1. Open STM32CubeIDE → *File → Import → Existing Projects into Workspace* and pick whichever project you want to try (e.g. `F4_CAN1_TX_RX`).
2. Wire up the hardware. The pinout is in the comment block at the top of that project's `main.c`.
3. Build and flash. For UART demos, open a terminal at 9600 8N1. For CAN, hook up a USB–CAN analyser or a second board running the matching project.
4. Read the driver source. That's really the point of the repo.

---

## 📂 Repository Layout

```
STM32F1-F4-Baremetal-Peripheral-Drivers/
├── F1_RCC_Clock_Config/        ── Blue Pill  · HSE+PLL → 72 MHz
├── F1_UART2_TX_RX/             ── Blue Pill  · USART2 echo
├── F1_SPI_W25Q32_Flash/        ── Blue Pill  · SPI1 + W25Q32 NOR flash
├── F1_I2C_LCD_16x2/            ── Blue Pill  · I²C1 + PCF8574 LCD
├── F1_CAN1_TX_RX/              ── Blue Pill  · bxCAN @ 500 kbps, IRQ RX
│
├── F4_RCC_Clock_180MHz/        ── Nucleo-F446RE · PLL + OverDrive → 180 MHz
├── F4_UART2_TX_RX/             ── Nucleo-F446RE · USART2 over ST-Link VCP
├── F4_SPI_W25Q32_Flash/        ── Nucleo-F446RE · SPI1 + W25Q32 NOR flash
├── F4_I2C_LCD_16x2/            ── Nucleo-F446RE · I²C1 + PCF8574 LCD
└── F4_CAN1_TX_RX/              ── Nucleo-F446RE · bxCAN @ 500 kbps, IRQ RX

each project/
├── Src/       main.c + <peripheral>_driver.c + syscalls / sysmem
├── Inc/       <peripheral>_driver.h + CMSIS device headers
├── Startup/   vector table & reset handler
└── *.ld       linker script
```

---

## 📜 License

MIT — see [`LICENSE`](LICENSE). Use any of this in your own projects, commercial or otherwise. Credit is appreciated but not required.

---

## 👤 About Me

I'm **Abhishek Hipparagi**, an embedded systems engineer based in Bangalore. Most recently I spent close to two years as an R&D Engineer at **Arvi Systems and Controls**, working on firmware for industrial UPS systems in the 10–400 kVA range — parallel redundancy (N+1) with Modbus RTU and CAN inter-unit communication, ESP32-based cloud telemetry gateways on FreeRTOS and ESP-IDF, TFT display firmware over RS-232, GSM/4G modem integration, and the usual mix of debugging ADC noise, RS-232 dropouts, and regulator issues that come with real hardware in the field. I led the peripheral-driver work for a platform migration from dsPIC33CK to dsPIC33AK/STM32, and designed 15+ PCBs along the way.

Before that I interned at **Plankt Biosystems** building STM32 + FreeRTOS firmware for automated microalgae inoculation, harvesting, and nutrient recirculation systems — three of those became organisation-level patents.

I work mainly with **dsPIC33, STM32 (F1/F4), and ESP32**, and I'm comfortable across **Modbus RTU, RS-232/485, CAN, GSM, and Wi-Fi (TCP/IP, HTTPS)**. I like firmware that's small, debuggable, and honest about what it's doing — which is basically why this repo exists.

If you're hiring for embedded firmware, bring-up, or driver work, I'd be happy to talk.

- 📍 Bangalore, India
- 📧 abhihipparagi11c@gmail.com
- 💼 [LinkedIn](https://linkedin.com/in/abhishek-hipparagi)
- 🐙 [GitHub](https://github.com/Abhishekhipparagi)
- ⭐ If this repo was useful to you, a star is the nicest thing you can do
