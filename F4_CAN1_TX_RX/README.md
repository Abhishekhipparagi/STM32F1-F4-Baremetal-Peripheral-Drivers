# F4 CAN1 TX/RX — Bare Metal

CAN1 driver for STM32F446RE Nucleo, 500 kbps Normal mode. Polling TX, interrupt RX with a user callback. No HAL.

Same public API as the F1 CAN driver — only the implementation differs. Drop-in compatible at the source level.

## Hardware

- **Board:** Nucleo-F446RE
- **MCU:** STM32F446RE, Cortex-M4 @ 180 MHz, APB1 @ 45 MHz (CAN1 lives here)
- **Bus speed:** 500 kbps, sample point 60%
- **Pins:** PB8 = CAN1_RX, PB9 = CAN1_TX, both **AF9**
- **LED:** PA5 (LD2, active HIGH)

| Nucleo pin | Arduino header | Signal |
|---|---|---|
| PB8 | D15 (CN5) | CAN1_RX -> transceiver RXD |
| PB9 | D14 (CN5) | CAN1_TX -> transceiver TXD |
| 3V3 | CN6-4 | transceiver VCC |
| GND | CN6-6 | transceiver GND |

```
+----------+        +-----------+        +--------------+
| Nucleo   |        | CAN xcvr  |        | CAN analyzer |
| D14/PB9 -+--TXD-->| TXD       |        |              |
| D15/PB8 <+--RXD---| RXD       |        |              |
|   3V3  --+--VCC-->| VCC   CANH+--+-----+ CANH         |
|   GND  --+--GND-->| GND   CANL+--+--+--+ CANL         |
+----------+        +-----------+  |  |  +--------------+
                                  120 120  (terminate
                                   ohm  ohm  BOTH ends)
```

**You need a transceiver chip** (MCP2551, SN65HVD230, TJA1050, etc.). The STM32 pins are TTL and can't drive a differential CAN bus directly.

**You need at least one other node on the bus** (analyzer, second MCU, anything that ACKs). Without an ACK, the TX error counter saturates after 256 attempts and the controller goes **bus-off**.

## ⚠ Pin choice: PB8/PB9 vs PA11/PA12

CAN1 on F446 can be mapped to two different pin sets, both AF9:

- **PA11/PA12** — on the Morpho headers, but they're shared with **USB OTG FS**. Even if you don't use USB, the routing on the Nucleo board can cause issues.
- **PB8/PB9** — on the **Arduino headers (D15/D14)**, no conflicts, easy breadboard access.

This driver uses **PB8/PB9**. If you need PA11/PA12 for some reason, change the GPIO setup in `can1_gpio_init()`.

## Architecture

```
+------------------+
|     main.c       |   test app
+--------+---------+
         |
+--------v---------+
| stm32f4_can_drv  |   peripheral driver (TX, RX, ISR, callback)
+--------+---------+
         |
   +-----v-----+
   |  CAN1 HW  |
   +-----------+
```

The driver owns the FIFO0 RX interrupt. The app registers a callback through `CAN1_RegisterRxCallback()` and the ISR fires it whenever a data frame arrives. Polling-mode `CAN1_Receive()` is also exposed if you don't want interrupts.

## Files

```
Inc/
  stm32f4_rcc_driver.h
  stm32f4_can_driver.h
Src/
  stm32f4_rcc_driver.c
  stm32f4_can_driver.c
  main.c
```

Pulls in the RCC driver from `F4_RCC_Clock_180MHz`. No UART driver needed — the bus + LED are enough feedback.

## Bit timing (500 kbps)

```
APB1 = 45 MHz

TQ      = BRP / APB1 = 18 / 45 MHz = 400 ns
bit     = (1 SYNC + TS1 + TS2) * TQ = (1 + 2 + 2) * 400 ns = 2 us
rate    = 1 / 2 us = 500 kbps
sample  = (1 + TS1) / (1 + TS1 + TS2) = 3/5 = 60%

BTR field values (stored as actual - 1):
  SJW = 1   ->  reg 0
  TS2 = 2   ->  reg 1
  TS1 = 2   ->  reg 1
  BRP = 18  ->  reg 17
```

These numbers match a tested HAL reference config exactly. **Sample point is 60%**, which is on the lower side — the textbook "safe" value is 87.5%. For 87.5% try `BRP=5, TS1=15, TS2=2` (gives 88.9%, close enough). I kept 60% so this driver matches a known-good configuration; experiment from there if you have noisy bus issues.

## Filter banks on F446

F446 has CAN1 + CAN2 sharing **28 filter banks**. The split point is set by `CAN2SB` in FMR:

- Banks `0 .. (CAN2SB-1)` belong to **CAN1**
- Banks `CAN2SB .. 27` belong to **CAN2**

This driver sets `CAN2SB = 20`, so banks 0-19 are CAN1's. The accept-all rule lives in **bank 18** (anywhere in 0-19 works; 18 matches the tested reference).

The F1 only has CAN1 and 14 filter banks, so this whole CAN2SB business doesn't exist there.

## Driver API

Identical to the F1 CAN driver:

```c
typedef enum { CAN_MODE_NORMAL, CAN_MODE_LOOPBACK,
               CAN_MODE_SILENT, CAN_MODE_SILENT_LOOPBACK } CAN_Mode_t;

typedef struct {
    uint32_t   prescaler;   uint8_t ts1, ts2, sjw;
    CAN_Mode_t mode;
} CAN_Config_t;

void         CAN1_GetDefault500kConfig(CAN_Config_t *cfg);
CAN_Status_t CAN1_Init(const CAN_Config_t *cfg);
void         CAN1_Filter_AcceptAll(uint8_t fifo);
CAN_Status_t CAN1_Start(void);

CAN_Status_t CAN1_Transmit(const CAN_TxHeader_t *hdr, const uint8_t *data, uint32_t *mailbox);
CAN_Status_t CAN1_Receive(uint32_t fifo, CAN_RxHeader_t *hdr, uint8_t *data);

typedef void (*CAN_RxCallback_t)(CAN_RxHeader_t *hdr, uint8_t *data);
void         CAN1_RegisterRxCallback(CAN_RxCallback_t cb);
```

## Usage

```c
RCC_SystemClock_Config_180MHz();

CAN_Config_t cfg;
CAN1_GetDefault500kConfig(&cfg);
CAN1_Init(&cfg);
CAN1_Filter_AcceptAll(CAN_RX_FIFO0);
CAN1_RegisterRxCallback(my_rx_handler);

if (CAN1_Start() != CAN_OK) {
    // bus dead, transceiver missing, etc.
}

CAN_TxHeader_t tx = { .std_id = 0x103, .ide = CAN_ID_STD,
                      .rtr = CAN_RTR_DATA, .dlc = 8 };
uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
uint32_t mailbox;
CAN1_Transmit(&tx, payload, &mailbox);
```

## How it works

**Init mode.** Bit timing register (BTR) is locked while CAN is running. `CAN1_Init()` sets `INRQ`, waits for `INAK`, clears `SLEEP`, waits for `SLAK=0`, then writes BTR. `CAN1_Start()` clears INRQ to leave init mode.

**Filters.** At least one bank must be **active** or every received frame is silently discarded. The driver configures bank 18 in 32-bit mask mode with `mask=0` -> "every bit is don't-care" -> accept everything. Real apps use specific ID/mask pairs to offload filtering to hardware.

**TX mailboxes.** Three of them. `CAN1_Transmit()` reads `TSR.CODE[1:0]` to find the next empty one, fills TIR/TDTR/TDLR/TDHR, then sets `TXRQ` to fire it. Setting TXRQ **last** is critical — set it earlier and the controller starts transmitting before you've finished loading the data.

**RX FIFO0.** Two FIFOs, 3 messages deep each. The driver writes `RFOM0` to release each slot after reading — forget that and the FIFO stays full forever.

**RX interrupt.** `FMPIE0` fires `CAN1_RX0_IRQHandler` whenever there's at least one message in FIFO0. Driver's ISR pulls the message out, releases the slot, and calls the registered callback. Callback runs in ISR context — keep it short, set a flag, do real work in the main loop.

**Bounded start.** `CAN1_Start()` has a timeout on the INAK wait. If the bus is dead or the transceiver is missing, the call returns `CAN_TIMEOUT` instead of hanging the boot. Main.c handles that with a fast-blink error indicator.

## F1 vs F4 — what changes

| | F103 | F446 |
|---|---|---|
| APB1 clock | 36 MHz | 45 MHz |
| Bit timing | retune (different APB1) | BRP=18, TS1=2, TS2=2 |
| GPIO regs | CRL/CRH | MODER/OTYPER/OSPEEDR/PUPDR/AFR |
| GPIO bus | APB2 | AHB1 |
| Pin pull-up | via ODR=1 trick | dedicated PUPDR register |
| AF for CAN | implicit (default mapping) | explicit AF9 in AFR |
| Default pins | PA11/PA12 | PB8/PB9 (avoid USB conflict) |
| Filter banks | 14 (CAN1 only) | 28 (shared CAN1 + CAN2) |
| Filter bank used | 0 | 18 (with CAN2SB=20) |
| RX0 ISR symbol | `USB_LP_CAN1_RX0_IRQHandler` | `CAN1_RX0_IRQHandler` |
| CAN MCR/MSR/BTR/TIR/RIR | — | identical |

The CAN peripheral itself is the same between families. Only the GPIO setup, the APB1 frequency, the filter-bank pool, and the ISR vector name change.

## Gotchas

- **No transceiver, no bus** — the MCU TX pin alone isn't a CAN bus. Use MCP2551 or similar.
- **No second node, immediate bus-off** — every TX needs an ACK. With nothing else listening, every frame errors out, TEC saturates, controller goes bus-off.
- **PA11/PA12 + USB** — the alternate pin mapping conflicts with USB OTG FS. Stick with PB8/PB9.
- **Filter bank choice** — picking a bank inside CAN2's range (with CAN2SB=20, that's banks 20-27) won't filter for CAN1. Driver picks bank 18 to stay safe.
- **Forgetting to leave sleep mode** — config writes "succeed" but nothing transmits.
- **Setting TXRQ before loading data** — sends garbage.
- **Wrong sample point on noisy buses** — 60% works on a clean breadboard but if you're getting random frame errors, try 87.5% (`BRP=5, TS1=15, TS2=2`).
- **Termination** — 120 ohm at *both ends* of the bus, not in the middle.

## Test

1. Wire transceiver between Nucleo PB8/PB9 (D15/D14) and the bus
2. Connect a CAN analyzer, set it to 500 kbps
3. Build, flash, run — you should see LD2 blink 3 times at boot, then start sending
4. Frames with **ID 0x103, DLC 8** every 500 ms, byte[0] counts 1..10
5. From the analyzer, send any frame with byte[0] = 5 -> LD2 blinks 5 times
6. If LD2 fast-blinks immediately after the boot sequence, `CAN1_Start()` timed out — check the transceiver wiring

## Build

STM32CubeIDE bare-metal project for STM32F446RETx. No HAL. Drop the driver pair + main.c into `Inc/` and `Src/`. Pull in the RCC driver from `F4_RCC_Clock_180MHz`. Confirm your startup file declares `CAN1_RX0_IRQHandler` (it should — that's the standard CMSIS name on F4).