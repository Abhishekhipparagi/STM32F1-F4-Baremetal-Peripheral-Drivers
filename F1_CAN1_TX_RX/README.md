# F1 CAN1 TX/RX — Bare Metal

CAN1 driver for STM32F103, 500 kbps Normal mode. Polling TX, interrupt RX with a user callback. No HAL.

## Hardware

- **Board:** Blue Pill (STM32F103C8T6 / C6T6)
- **MCU:** Cortex-M3 @ 72 MHz, APB1 @ 36 MHz (CAN1 lives here)
- **Bus speed:** 500 kbps, sample point 87.5%
- **Pins:** PA11 = CAN_RX, PA12 = CAN_TX (default mapping, no AFIO remap)
- **LED:** PC13 (onboard, active LOW)

```
+----------+        +-----------+        +--------------+
| STM32    |        | CAN xcvr  |        | CAN analyzer |
|   PA12 --+--TXD-->| TXD       |        |              |
|   PA11 <-+--RXD---| RXD       |        |              |
|   3V3  --+--VCC-->| VCC   CANH+--+-----+ CANH         |
|   GND  --+--GND-->| GND   CANL+--+--+--+ CANL         |
+----------+        +-----------+  |  |  +--------------+
                                  120 120  (terminate
                                   ohm  ohm  BOTH ends)
```

**You need a transceiver chip** (MCP2551, SN65HVD230, TJA1050, etc.). The STM32 pins are TTL — they can't drive a differential CAN bus directly. The transceiver also handles the ISO 11898 voltage levels and bus protection.

**You need at least one other node on the bus** (another MCU, a CAN analyzer, anything that ACKs). CAN frames must be acknowledged by at least one receiver, otherwise the transmitter sees an ACK error, the TX error counter (TEC) climbs, and after 256 errors the controller goes **bus-off** and stops transmitting entirely.

## Architecture

```
+------------------+
|     main.c       |   test app
+--------+---------+
         |
+--------v---------+
| stm32f1_can_drv  |   peripheral driver (TX, RX, ISR, callback)
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
  stm32f1_rcc_driver.h
  stm32f1_can_driver.h
Src/
  stm32f1_rcc_driver.c
  stm32f1_can_driver.c
  main.c
```

Pulls in the RCC driver from `F1_RCC_Clock_Config`. No UART driver needed for this one — feedback is over the CAN bus itself + the LED.

## Bit timing (500 kbps)

```
APB1 = 36 MHz

TQ      = BRP / APB1 = 9 / 36 MHz = 250 ns
bit     = (1 SYNC + TS1 + TS2) * TQ = (1 + 6 + 1) * 250 ns = 2 us
rate    = 1 / 2 us = 500 kbps
sample  = (1 + TS1) / (1 + TS1 + TS2) = 7/8 = 87.5%

BTR field values (stored as actual - 1):
  SJW = 1  ->  reg 0
  TS2 = 1  ->  reg 0
  TS1 = 6  ->  reg 5
  BRP = 9  ->  reg 8
```

`CAN1_GetDefault500kConfig()` sets these. To run a different rate, fill `CAN_Config_t` yourself — TS1+TS2 stay around 8 TQ for 87.5% sample point and you change BRP to scale the rate.

## Driver API

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
RCC_SystemClock_Config_72MHz();

CAN_Config_t cfg;
CAN1_GetDefault500kConfig(&cfg);
CAN1_Init(&cfg);
CAN1_Filter_AcceptAll(CAN_RX_FIFO0);
CAN1_RegisterRxCallback(my_rx_handler);
CAN1_Start();

CAN_TxHeader_t tx = { .std_id = 0x103, .ide = CAN_ID_STD,
                      .rtr = CAN_RTR_DATA, .dlc = 8 };
uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
uint32_t mailbox;
CAN1_Transmit(&tx, payload, &mailbox);
```

## How it works

**Init mode.** The bit timing register (BTR) is locked while the CAN controller is running. You enter init mode by setting `INRQ` in MCR, wait for `INAK` to acknowledge, write BTR, then leave init mode in `CAN1_Start()`. Try to write BTR after the controller is running and the write is silently dropped.

**Sleep mode.** The chip powers up in sleep. You have to clear the `SLEEP` bit and wait for `SLAK=0` to acknowledge before doing anything useful. Easy to forget.

**Filters.** At least one filter bank must be **active** or every received frame is silently discarded. The driver configures filter 0 in 32-bit mask mode with mask=0, which means "every bit is don't-care" -> accept everything. Real apps would use specific ID/mask pairs to offload filtering to hardware.

**TX mailboxes.** There are 3 of them. `CAN1_Transmit()` reads `TSR.CODE[1:0]` to find the next empty one, fills TIR/TDTR/TDLR/TDHR, then sets `TXRQ` to fire it. Setting TXRQ **last** is critical — set it earlier and the controller starts transmitting before you've finished loading the data.

**RX FIFO0.** Two FIFOs, 3 messages deep each. Filter routes messages to one or the other. When a message is read, the driver writes `RFOM0` to release the slot — if you forget that, the FIFO stays full forever and new messages get dropped.

**RX interrupt.** `FMPIE0` fires the `USB_LP_CAN1_RX0_IRQHandler` whenever there's at least one message in FIFO0. The driver's ISR pulls the message out, releases the slot, and calls the registered callback. Callback runs in ISR context — keep it short, set a flag, do real work in the main loop.

## F1 vs F4 — what changes

| | F103 | F446 |
|---|---|---|
| APB1 clock | 36 MHz | 45 MHz |
| Bit timing | retune BRP/TS1/TS2 | 45 MHz needs different splits |
| GPIO regs | CRL/CRH | MODER/OTYPER/OSPEEDR/PUPDR/AFR |
| GPIO bus | APB2 | AHB1 |
| AF for CAN | implicit (default mapping) | AF9 in AFR |
| ISR symbol | `USB_LP_CAN1_RX0_IRQHandler` | `CAN1_RX0_IRQHandler` |
| CAN MCR/MSR/BTR/TIR/RIR | — | identical |

The CAN peripheral itself is the same between families. Only the GPIO setup, the APB1 frequency (which forces a different prescaler), and the ISR vector name change.

## Gotchas

- **No transceiver, no bus** — the MCU TX pin alone isn't a CAN bus. You need MCP2551 or similar.
- **No second node, immediate bus-off** — every TX needs an ACK. With nothing else listening, every frame errors out, TEC saturates after 256 attempts, controller goes bus-off.
- **Forgetting to leave sleep mode** — config writes "succeed" but nothing transmits.
- **Forgetting to activate the filter** — RX is silent.
- **Setting TXRQ before loading data** — sends garbage.
- **ISR symbol mismatch** — on F103 the official name is `USB_LP_CAN1_RX0_IRQHandler` (the vector is shared with USB Low Priority). If your startup file uses a slightly different name, the ISR never gets called and RX looks broken even though everything else is right. Check `startup_stm32f103xxxx.s`.
- **Not releasing the FIFO** — chip keeps the message, FIFO fills up at 3 deep, FIFO overrun, data lost.
- **Termination resistors** — 120 ohm at *both* ends of the bus, not in the middle. Wrong termination causes reflections and intermittent frame errors that look like baud rate issues.

## Test

1. Wire transceiver between Blue Pill PA11/PA12 and the bus
2. Connect a CAN analyzer to the bus, set it to 500 kbps
3. Build, flash, run
4. You should see frames with **ID 0x103, DLC 8** arriving every 500 ms, byte[0] counts 1..10
5. From the analyzer, send any frame with byte[0] = 5 -> the LED blinks 5 times
6. Send byte[0] = 3 -> 3 blinks. Etc.

## Build

STM32CubeIDE bare-metal project for STM32F103C8Tx (or C6Tx). No HAL. Drop the driver pair + main.c into `Inc/` and `Src/`. Pull in the RCC driver from `F1_RCC_Clock_Config`. Confirm your startup file declares `USB_LP_CAN1_RX0_IRQHandler` as the FIFO0 vector.