# F1 USART2 TX/RX — Bare Metal

USART2 driver for STM32F103 Blue Pill. TX polling, RX interrupt-driven with a 128-byte ring buffer. Classic loopback echo demo — type into a terminal and the board types back. No HAL.

## Hardware

- **Board:** Blue Pill (STM32F103C8T6 / C6T6)
- **MCU:** Cortex-M3 @ 72 MHz
- **USART2:** PA2 = TX, PA3 = RX
- **Baud:** 9600 8N1
- **LED:** PC13 (onboard, active LOW)

USART2 lives on APB1 = 36 MHz, so that's what goes into the BRR calculation.

```
+----------+        +----------+
| STM32    |        | USB-TTL  |
|   PA2 ---+--TX--->| RX       |
|   PA3 <--+--RX----| TX       |
|   GND  --+--------+ GND      |
+----------+        +----------+
```

Standard USB-to-serial adapter. Make sure the adapter is 3.3 V tolerant or use level shifting. PC13 on the onboard LED toggles on every received byte so you can see RX is actually firing even before you look at the terminal.

## Files

```
Inc/
  stm32f1_rcc_driver.h
  stm32f1_uart_driver.h
Src/
  stm32f1_rcc_driver.c
  stm32f1_uart_driver.c
  main.c
```

Pulls in the RCC driver from `F1_RCC_Clock_Config` to set up the 72 MHz clock tree.

## BRR math (9600 baud)

```
APB1 = 36 MHz
USARTDIV = 36,000,000 / (16 * 9600) = 234.375

mantissa = 234       (register bits [15:4])
fraction = 0.375 * 16 = 6    (register bits [3:0])

BRR = (234 << 4) | 6 = 0x0EA6
```

Integer-only math, no floats. The driver computes this at init time from whatever baud you pass in — so changing rate is just a parameter, not a register edit.

## Driver API

```c
void    UART2_Init(uint32_t baudrate);

void    UART2_SendChar(char c);           // blocking TX
void    UART2_SendString(const char *str);

uint8_t UART2_DataAvailable(void);         // check ring buffer
uint8_t UART2_GetChar(void);               // pull one byte
```

TX is polling — the driver waits on `TXE` (DR empty), writes, then waits on `TC` (byte fully on the wire). RX is interrupt-driven — the driver's `USART2_IRQHandler` reads DR on `RXNE` and pushes the byte into a 128-byte ring buffer. Main loop polls `DataAvailable()` and pulls bytes out with `GetChar()`.

## Usage

```c
RCC_SystemClock_Config_72MHz();
UART2_Init(9600);

UART2_SendString("Hello\r\n");

while (1) {
    if (UART2_DataAvailable()) {
        char c = UART2_GetChar();
        UART2_SendChar(c);         // echo
    }
}
```

## How it works

**TX (polling).** `SendChar` blocks until the hardware TX register is empty (`TXE=1`), writes the byte into `DR`, and waits until the last bit has physically left the pin (`TC=1`). `TXE` clears as soon as the byte moves from DR into the shift register — for back-to-back sending that's enough. `TC` waiting only matters for the last byte in a burst (e.g. before flipping RS-485 direction or entering sleep mode).

**RX (interrupt + ring buffer).** `RXNEIE` is enabled in `CR1`, so `USART2_IRQHandler` fires the moment a byte lands in DR. The ISR reads DR (which clears `RXNE`) and pushes the byte into the ring buffer:

```
     head (ISR writes here)
     v
[ H  E  L  L  O  .  .  . ]
 ^
 tail (main reads here)
```

`head` and `tail` are both `volatile uint16_t`. The ISR only ever writes `head`, main only ever writes `tail` — single-producer, single-consumer, so no mutex or `__disable_irq()` needed. If the buffer fills up (next head would collide with tail), the incoming byte is dropped rather than overwriting the oldest one. For a 9600 baud terminal the main loop drains the buffer fast enough that overflow never happens.

**Why interrupt for RX and polling for TX?** You control *when* a byte gets sent — it's your code calling `SendChar`, so blocking briefly on `TXE` is fine. You don't control when a byte arrives — if the CPU is busy doing something else and you miss the narrow window after `RXNE` sets, the next incoming byte overwrites the old one in DR and you get an overrun (`ORE` flag, data lost). An interrupt is the only way to guarantee you catch every byte.

## Overrun — the thing everyone hits once

If the ISR is too slow or interrupts are disabled too long:

```
byte 'A' arrives -> DR = 'A', RXNE = 1
(CPU still busy...)
byte 'B' arrives -> DR = 'B', ORE = 1   <- 'A' is gone forever
```

Prevention is really just: keep the ISR short (read DR, push to buffer, return — no delays, no prints, no heavy work), and don't sit with interrupts disabled. The driver follows both rules.

## GPIO config (F1 style)

F1 packs direction and function into 4 bits per pin in `CRL`/`CRH`:

- **PA2 (TX):** `MODE = 11` (50 MHz output) + `CNF = 10` (alternate function push-pull). The USART2 hardware drives the pin — your code never writes PA2's ODR.
- **PA3 (RX):** `MODE = 00` (input) + `CNF = 01` (floating input). The external device drives the line; we just listen.

Push-pull (not open-drain) for TX because UART is point-to-point and the line idles HIGH — push-pull gives a clean HIGH without needing external pull-ups.

## F1 vs F4 — what actually changes for UART

| | F103 | F446 |
|---|---|---|
| APB1 clock | 36 MHz | 45 MHz |
| BRR for 9600 | `0x0EA6` | `0x124F` |
| GPIO regs | CRL/CRH | MODER/OTYPER/OSPEEDR/PUPDR/AFR |
| GPIO bus | APB2 | AHB1 |
| AF mux | fixed (or AFIO remap) | explicit AF7 in AFR |
| RX pin mode | "floating input" | AF mode (same as TX) |
| SR/DR/CR1/BRR | — | identical |

The USART peripheral itself is the same. Only the GPIO wiring around it differs. BRR computation is the same formula, just with a different clock.

## Gotchas

- **Writing BRR while UE=1** — undefined behavior. Always `CR1 = 0` first, set BRR, then enable UE + TE + RE.
- **Heavy work in the ISR** — don't call `SendString`, don't `delay_ms`. Read DR, push, return.
- **Forgetting to read DR in the ISR** — `RXNE` stays set, the interrupt re-fires immediately on return, you sit in a tight ISR loop forever.
- **Baud mismatch** — 2-3% is tolerable, more than that and the receiver samples at the wrong time and you get framing errors + garbage.
- **Misnamed ISR symbol** — if the handler in the .c file isn't exactly `USART2_IRQHandler`, the linker silently keeps the weak `Default_Handler` from the startup file and RX looks completely dead. Match the startup file name exactly.

## Test

1. Wire USB-TTL adapter (PA2→RX, PA3→TX, GND→GND)
2. Open a terminal at **9600 8N1** on the adapter's COM port
3. Flash and reset the board
4. You should see:
   ```
   UART Ready!
   Type something, it will echo back.
   ```
5. Type characters — they echo back, PC13 LED toggles on each byte
6. Press **Enter** — the board responds with `You pressed Enter!`

## Build

STM32CubeIDE bare-metal project for STM32F103C6Tx (or C8Tx). No HAL. Drop the driver pair + `main.c` into `Inc/` and `Src/`. Pull in the RCC driver from `F1_RCC_Clock_Config`. Delete the auto-generated `main.c` that CubeIDE creates before copying mine in — duplicate symbols will break the link otherwise.