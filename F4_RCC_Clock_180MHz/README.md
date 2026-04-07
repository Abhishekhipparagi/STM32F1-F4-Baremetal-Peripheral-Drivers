# F4_RCC_Clock_180MHz

Bare-metal RCC clock configuration for STM32F446RE (Nucleo board). Configures the system clock to 180 MHz using HSE + PLL with voltage scaling and over-drive, all at register level — no HAL.

## What it does

```
HSE(8MHz) → /PLLM(4) → 2MHz → *PLLN(180) → 360MHz → /PLLP(2) → SYSCLK(180MHz)
                                                    → /PLLQ(8) → USB(45MHz)

SYSCLK(180MHz) → AHB(÷1) → HCLK(180MHz)
                  ├→ APB1(÷4) → PCLK1(45MHz)  → timers get x2 = 90MHz
                  └→ APB2(÷2) → PCLK2(90MHz)  → timers get x2 = 180MHz
```

## What's different from F103

| | F103 | F446 |
|---|---|---|
| Max clock | 72 MHz | 180 MHz |
| PLL | just a multiplier | M, N, P, Q params |
| Flash WS | 2 max | 5 max |
| Caches | prefetch only | I-cache + D-cache |
| Voltage scaling | no | yes, via PWR |
| Over-drive | no | required above 168 MHz |
| HSI | 8 MHz | 16 MHz |
| GPIO config | CRL/CRH | MODER/OSPEEDR/AFR |
| GPIO clock bus | APB2 | AHB1 |
| MCO prescaler | none (PLL/2 only) | ÷2 to ÷5 |

## Hardware

- NUCLEO-F446RE board
- HSE: 8 MHz from ST-Link MCO output (not a separate crystal)
- LED: PA5 (active HIGH)
- MCO1: PA8

## Files

```
Inc/
  stm32f4_rcc_driver.h
Src/
  stm32f4_rcc_driver.c
  main.c
```

## Driver API

```c
// direct 180 MHz — same register sequence tested on hardware
RCC_SystemClock_Config_180MHz();

// or flexible struct-based
RCC_ClkConfig_t clk;
RCC_GetDefault180MHzConfig(&clk);
RCC_Init(&clk);

// HSI fallback (16 MHz on F4, not 8!)
RCC_SystemClock_Config_HSI();

// MCO1 on PA8 — F4 has a prescaler, PLL/4 = 45 MHz
RCC_MCO1_Init(RCC_MCO1_PLL, RCC_MCO1_PRE_DIV4);

// read back
uint32_t freq = RCC_GetSysClockFreq();  // 180000000
```

## 180 MHz setup sequence

1. Enable HSE, wait for HSERDY
2. Enable PWR clock, set voltage scale 1 (F4 only)
3. Set flash to 5 wait states + I-cache + D-cache + prefetch
4. Set bus prescalers (AHB ÷1, APB1 ÷4, APB2 ÷2)
5. Configure PLL: M=4, N=180, P=2, Q=8, source=HSE — PLL must be OFF
6. Enable PLL, wait for PLLRDY
7. Enable over-drive + switch to OD mode (F4 only, required for >168 MHz)
8. Switch SYSCLK to PLL, verify via SWS

## MCO1 output

PA8 outputs PLL / 4 = 45 MHz. Unlike F103 where MCO was hardwired to PLL/2, F446 has a configurable prescaler (÷2 to ÷5) so you can pick a frequency that the pin can cleanly reproduce.

## How to verify

| Method | What to check |
|--------|--------------|
| LED blink | PA5 blinks at ~1 Hz = 180 MHz OK |
| MCO1 (PA8) | Scope shows ~45 MHz (PLL/4) |
| Debugger | `actual_freq` should read 180000000 |

## Build

STM32CubeIDE project targeting STM32F446RE. Open, build, flash.

## References

- RM0390 Reference Manual — RCC, Flash, PWR sections
- STM32F446xC/E Datasheet
