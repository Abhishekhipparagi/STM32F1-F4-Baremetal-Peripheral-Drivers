/*
 * stm32f1_spi_driver.h — SPI1 master driver for STM32F103, bare metal
 *
 * SPI1 pins (default mapping, no remap):
 *   PA5 = SCK
 *   PA6 = MISO
 *   PA7 = MOSI
 *   CS is NOT handled here — device driver owns its own CS pin
 *
 * SPI1 sits on APB2 = 72 MHz (when SYSCLK = 72 MHz).
 * SPI2 would be on APB1 = 36 MHz — different prescaler math.
 *
 * Mode 0 only (CPOL=0, CPHA=0), 8-bit, MSB first, software NSS.
 * That covers W25Q32, most SD cards, many sensors. Add modes if needed.
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F1_SPI_DRIVER_H
#define STM32F1_SPI_DRIVER_H

#include "stm32f1xx.h"
#include <stdint.h>

// BR[2:0] -> APB2 prescaler. APB2 = 72 MHz on F103 when running at full speed.
typedef enum {
    SPI_BR_DIV2   = 0x00,   // 36 MHz
    SPI_BR_DIV4   = 0x01,   // 18 MHz — safe default for breadboards
    SPI_BR_DIV8   = 0x02,   // 9  MHz
    SPI_BR_DIV16  = 0x03,
    SPI_BR_DIV32  = 0x04,
    SPI_BR_DIV64  = 0x05,
    SPI_BR_DIV128 = 0x06,
    SPI_BR_DIV256 = 0x07
} SPI_BaudDiv_t;

// configures PA5/PA6/PA7 + SPI1 peripheral, master mode 0, software NSS
void    SPI1_Init(SPI_BaudDiv_t baud_div);

// full-duplex byte transfer — send one, get one back
// to read: send 0xFF, use the return value
// to write: send your byte, ignore the return value
uint8_t SPI1_TransmitReceive(uint8_t data);

#endif /* STM32F1_SPI_DRIVER_H */
