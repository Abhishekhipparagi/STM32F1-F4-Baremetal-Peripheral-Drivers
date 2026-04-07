/*
 * stm32f1_can_driver.h — CAN1 driver for STM32F103, bare metal
 *
 * Default pin mapping (no AFIO remap):
 *   PA11 = CAN_RX  (input pull-up)
 *   PA12 = CAN_TX  (AF push-pull)
 *
 * CAN1 sits on APB1 = 36 MHz (when SYSCLK = 72 MHz). Bit timing
 * is computed from APB1, so any timing change starts here.
 *
 * 500 kbps default: BRP=9, TS1=6, TS2=1, SJW=1, sample point 87.5%
 * (8 TQ per bit @ 36 MHz / 9 = 250 ns/TQ -> 2 us bit -> 500 kbps)
 *
 * A CAN transceiver chip (MCP2551, SN65HVD230, TJA1050) is REQUIRED
 * between the MCU and the bus. The MCU pins are TTL; the bus is
 * differential CANH/CANL. And there must be at least one OTHER node
 * on the bus to ACK frames, or every TX fails -> bus-off.
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F1_CAN_DRIVER_H
#define STM32F1_CAN_DRIVER_H

#include "stm32f1xx.h"
#include <stdint.h>

// --- status / enums ---

typedef enum {
    CAN_OK      = 0x00,
    CAN_ERROR   = 0x01,
    CAN_TIMEOUT = 0x02
} CAN_Status_t;

// operating mode -- maps to LBKM/SILM bits in BTR
typedef enum {
    CAN_MODE_NORMAL          = 0x00,   // TX + RX on physical bus
    CAN_MODE_LOOPBACK        = 0x01,   // TX looped back internally, no bus needed
    CAN_MODE_SILENT          = 0x02,   // listen only, no TX
    CAN_MODE_SILENT_LOOPBACK = 0x03    // self-test, no bus activity
} CAN_Mode_t;

// IDE / RTR bit values match register positions in TIR/RIR
#define CAN_ID_STD       0x00U          // standard 11-bit identifier
#define CAN_ID_EXT       0x04U          // extended 29-bit identifier
#define CAN_RTR_DATA     0x00000000U    // data frame (carries payload)
#define CAN_RTR_REMOTE   0x00000002U    // remote frame (request, no payload)

#define CAN_RX_FIFO0     0x00U
#define CAN_RX_FIFO1     0x01U

// --- bit timing config ---
//
// register fields are stored as (actual_value - 1), see CAN_Init().
// see GetDefault500kConfig() for the standard 500 kbps numbers.
typedef struct {
    uint32_t   prescaler;   // BRP, 1-1024
    uint8_t    ts1;         // TS1 (Prop_Seg + Phase_Seg1), 1-16
    uint8_t    ts2;         // TS2 (Phase_Seg2), 1-8
    uint8_t    sjw;         // SJW, 1-4
    CAN_Mode_t mode;
} CAN_Config_t;

// --- TX / RX message headers ---
//
// fill TxHeader before calling CAN_Transmit. Driver fills RxHeader.
typedef struct {
    uint32_t std_id;        // 11-bit, used when ide == CAN_ID_STD
    uint32_t ext_id;        // 29-bit, used when ide == CAN_ID_EXT
    uint32_t ide;           // CAN_ID_STD or CAN_ID_EXT
    uint32_t rtr;           // CAN_RTR_DATA or CAN_RTR_REMOTE
    uint32_t dlc;           // 0-8 data bytes
} CAN_TxHeader_t;

typedef struct {
    uint32_t std_id;
    uint32_t ext_id;
    uint32_t ide;
    uint32_t rtr;
    uint32_t dlc;
    uint32_t timestamp;
    uint32_t filter_match_index;
} CAN_RxHeader_t;

// optional RX callback — driver's ISR calls this when a frame arrives
// keep it short, no delays, no blocking work
typedef void (*CAN_RxCallback_t)(CAN_RxHeader_t *header, uint8_t *data);

// --- API ---

// init CAN1 (GPIO + bit timing). Stays in init mode until CAN_Start().
CAN_Status_t CAN1_Init(const CAN_Config_t *cfg);

// fill cfg with the standard 500 kbps numbers (8 TQ, 87.5% sample point)
void CAN1_GetDefault500kConfig(CAN_Config_t *cfg);

// configure filter bank 0 to accept ALL frames into the given FIFO
void CAN1_Filter_AcceptAll(uint8_t fifo);

// exit init mode (join the bus) and enable the FIFO0 RX interrupt
CAN_Status_t CAN1_Start(void);

// register a callback that the driver's ISR will invoke on every
// data frame in FIFO0. Pass NULL to disable.
void CAN1_RegisterRxCallback(CAN_RxCallback_t cb);

// queue a frame for transmission. Returns CAN_OK if accepted by a
// mailbox, CAN_ERROR if all 3 mailboxes are full.
CAN_Status_t CAN1_Transmit(const CAN_TxHeader_t *header,
                           const uint8_t *data,
                           uint32_t *mailbox);

// pull one frame out of the given FIFO. Polling-mode alternative
// to using the callback. Returns CAN_OK if a message was read.
CAN_Status_t CAN1_Receive(uint32_t fifo,
                          CAN_RxHeader_t *header,
                          uint8_t *data);

#endif /* STM32F1_CAN_DRIVER_H */
