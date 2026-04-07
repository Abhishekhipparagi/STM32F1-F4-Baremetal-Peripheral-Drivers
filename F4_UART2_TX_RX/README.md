# F4_UART2_TX_RX

Bare-metal USART2 driver for STM32F446RE (Nucleo). TX polling, RX interrupt + ring buffer. Register-level, no HAL.

## What it does

UART echo — characters typed on terminal echo back. PA5 LED toggles on each received byte.

```
STM32F446RE        USB-TTL (or just use Nucleo's built-in ST-Link COM port)
┌──────────┐       ┌──────────┐
│  PA2 TX  ├──────►│ RX       │
│  PA3 RX  │◄──────┤ TX       │
│  GND     ├───────┤ GND      │
└──────────┘       └──────────┘
```

On Nucleo boards, PA2/PA3 are already wired to the ST-Link virtual COM port — just plug in USB and open a terminal. No extra adapter needed.

## Config

- USART2 on APB1 (45 MHz)
- 9600 baud, 8N1
- PA2 = TX, PA3 = RX, both AF7

## What's different from F103 UART

The USART peripheral itself (SR, DR, BRR, CR1) is basically identical between F103 and F446. The ISR, ring buffer, TX polling — all the same code. What changes:

| | F103 | F446 |
|---|---|---|
| APB1 clock | 36 MHz | 45 MHz |
| GPIO config | CRL/CRH (MODE+CNF) | MODER/OSPEEDR/AFR |
| GPIO clock bus | APB2 | AHB1 |
| AF selection | implicit (fixed mapping) | explicit AFR register, AF7 for USART2 |
| RX pin mode | floating input | AF mode (peripheral decides direction) |
| LED pin | PC13 active LOW | PA5 active HIGH |

## Files

```
Inc/
  stm32f4_uart_driver.h
  stm32f4_rcc_driver.h      — clock config (180 MHz)
Src/
  stm32f4_uart_driver.c
  stm32f4_rcc_driver.c
  main.c
```

## Driver API

```c
UART2_Init(9600);

UART2_SendChar('A');
UART2_SendString("hello\r\n");

if (UART2_DataAvailable()) {
    uint8_t byte = UART2_GetChar();
}
```

## BRR at 45 MHz

```
USARTDIV = 45,000,000 / (16 × 9,600) = 292.97
mantissa = 292, fraction ≈ 15
BRR = (292 << 4) | 15 = 0x124F
```

Same formula as F103, just different input clock.

## AF7 — the gotcha when porting from F103

F103 had fixed pin mapping — PA2 was always USART2_TX, no configuration needed. F446 has a configurable AF mux with 16 options per pin. You *must* write AF7 into `GPIOA->AFR[0]` for PA2 and PA3, or USART2 never connects to the pins. Everything else looks correct but nothing works. Easy to miss when porting.

## How to test

1. Plug Nucleo USB cable (uses built-in ST-Link COM port)
2. Open terminal: 9600 baud, 8N1
3. Type characters — they echo back
4. Press Enter — prints "You pressed Enter!"
5. PA5 LED toggles on each byte

## Build

STM32CubeIDE project targeting STM32F446RE. Needs the F4 RCC driver files for 180 MHz clock.

## References

- RM0390 Reference Manual — USART, GPIO sections
- STM32F446xC/E Datasheet — AF mapping table