/*
 * stm32f4_spi_driver.h — SPI1 master driver for STM32F446RE, bare metal
 *
 * SPI1 default pin mapping (no remap):
 *   PA5 = SCK   (AF5)
 *   PA6 = MISO  (AF5)
 *   PA7 = MOSI  (AF5)
 *   CS is NOT handled here — device driver owns its own CS pin
 *
 * SPI1 sits on APB2 = 90 MHz (when SYSCLK = 180 MHz).
 * SPI2/SPI3 are on APB1 = 45 MHz — different prescaler math.
 *
 * Mode 0 only (CPOL=0, CPHA=0), 8-bit, MSB first, software NSS.
 *
 * NOTE on Nucleo-F446RE: PA5 is also tied to LD2 (onboard LED).
 * You can't use both at once. Either move the LED to another pin
 * or remap SPI1 to PB3/PB4/PB5.
 *
 * F1 vs F4 — what differs:
 *   - GPIO regs: MODER/OTYPER/OSPEEDR/AFR (not CRL/CRH)
 *   - GPIO clock on AHB1 (not APB2)
 *   - AF mux: must explicitly set AF5 for SPI1 in AFR
 *   - APB2 is 90 MHz here, 72 MHz on F1 — same prescaler, different speed
 *   - SPI peripheral itself (CR1, SR, DR) is identical
 *
 * Author: Abhishek Hipparagi
*/

#ifndef STM32F4_SPI_DRIVER_H
#define STM32F4_SPI_DRIVER_H

#include "stm32f4xx.h"
#include <stdint.h>

// BR[2:0] -> APB2 divider. APB2 = 90 MHz on F446 at full speed.
typedef enum {
    SPI_BR_DIV2   = 0x00,   // 45   MHz
    SPI_BR_DIV4   = 0x01,   // 22.5 MHz — safe default for breadboards
    SPI_BR_DIV8   = 0x02,   // 11.25 MHz
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

#endif /* STM32F4_SPI_DRIVER_H */
