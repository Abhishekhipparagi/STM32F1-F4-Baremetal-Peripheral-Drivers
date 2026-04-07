# F1_RCC_Clock_Config

Bare-metal RCC clock configuration for STM32F103C6 (Blue Pill). Configures the system clock to 72 MHz using HSE + PLL, all at register level — no HAL.

## What it does

Sets up the full clock tree:

```
HSE(8MHz) → PLL(×9) → SYSCLK(72MHz) → AHB(÷1) → HCLK(72MHz)
                                        ├→ APB1(÷2) → PCLK1(36MHz)
                                        └→ APB2(÷1) → PCLK2(72MHz)
```

Outputs PLL/2 on the MCO pin (PA8) so you can verify the clock with a scope, and blinks the onboard LED (PC13) at 1 Hz as a quick sanity check.

## Hardware

- STM32F103C6T6 Blue Pill
- 8 MHz crystal on OSC_IN/OSC_OUT

## Files

```
Inc/
  stm32f1_rcc_driver.h    — driver header (config struct, enums, API)
Src/
  stm32f1_rcc_driver.c    — driver implementation
  main.c                  — example usage
```

## Driver API

```c
// quickest way — hardcoded 72 MHz, same sequence tested on hardware
RCC_SystemClock_Config_72MHz();

// or use the config struct for custom setups
RCC_ClkConfig_t clk;
RCC_GetDefault72MHzConfig(&clk);
RCC_Init(&clk);

// fallback if crystal is missing
RCC_SystemClock_Config_HSI();

// MCO output on PA8 for scope verification
RCC_MCO_Init(RCC_MCO_PLL_DIV2);

// read back actual frequencies from registers
uint32_t freq = RCC_GetSysClockFreq();  // 72000000
uint32_t apb1 = RCC_GetPCLK1Freq();    // 36000000
```

## Clock config sequence

Order matters — get it wrong and the chip hard faults or hangs:

1. Enable HSI, switch to it (safe fallback)
2. Enable HSE, wait for HSERDY
3. Set flash wait states (2 WS for 72 MHz) — **before** increasing clock
4. Configure PLL source (HSE) and multiplier (×9) — PLL must be OFF
5. Enable PLL, wait for PLLRDY
6. Set bus prescalers (AHB ÷1, APB1 ÷2, APB2 ÷1)
7. Switch SYSCLK to PLL, confirm via SWS bits

If you switch to PLL before setting flash latency, the CPU reads garbage from flash and crashes. Found that out the hard way.

## MCO pin limitation

The MCO output on PA8 is configured at 50 MHz GPIO speed, but in practice the pin's slew rate can't cleanly reproduce signals much above ~15 MHz. At higher frequencies you'll see a triangle wave instead of a square wave on the scope.

That's why PLL/2 (36 MHz) is used instead of SYSCLK directly — even 36 MHz is pushing it, you'll notice the edges are rounded. To verify the full 72 MHz you'd need to check the SWS bits in RCC_CFGR via the debugger, or use `RCC_GetSysClockFreq()` which reads the actual register values.

## How to verify

| Method | What to check |
|--------|--------------|
| LED blink | PC13 blinks at ~1 Hz = 72 MHz OK. Much slower = still on 8 MHz HSI |
| MCO (PA8) | Scope should show ~36 MHz (PLL/2). Waveform won't be a clean square — that's the pin speed limit, not a clock problem |
| Debugger | Add `actual_freq` to watch window. Should read 72000000 |
| `RCC_GetSysClockFreq()` | Returns SYSCLK decoded from SWS + PLLMULL registers |

## APB1 note

APB1 max is 36 MHz. Don't set the prescaler to ÷1 at 72 MHz or you'll overclock the APB1 peripherals (UART2/3, I2C, CAN, SPI2).

Timer clock quirk: when APB1 prescaler ≠ 1, the timer clocks get doubled automatically. So TIM2/3/4 still run at 72 MHz even though PCLK1 is 36 MHz.

## Build

STM32CubeIDE project. Open, build, flash. No external dependencies beyond CMSIS headers.

## References

- RM0008 Reference Manual — Sec 7 (RCC), Sec 3 (Flash)
- STM32F103x6 Datasheet
