/*
 * w25q32_driver.h — Winbond W25Q32 SPI NOR flash driver
 *
 * 32 Mbit (4 MB) SPI NOR flash, 24-bit addressing.
 * Sits on top of stm32f1_spi_driver — bus is SPI1, CS is PA4 (manual GPIO).
 *
 * Why manual CS instead of hardware NSS?
 *   Hardware NSS toggles between every byte. W25Q commands need CS held
 *   LOW for the entire opcode + address + data sequence.
 *
 * Usage pattern:
 *   W25Q_Init();
 *   id = W25Q_ReadJEDECID();        // expect 0xEF4016 for W25Q32
 *   W25Q_SectorErase(addr);         // erase before write — flash can only 1->0
 *   W25Q_PageProgram(addr, buf, n); // max 256 bytes, can't cross page boundary
 *   W25Q_ReadData(addr, buf, n);    // any length, no alignment
 *
 * Author: Abhishek Hipparagi
 */

#ifndef W25Q32_DRIVER_H
#define W25Q32_DRIVER_H

#include <stdint.h>

#define W25Q32_JEDEC_ID         0x00EF4016U     // Winbond + NOR + 32Mbit
#define W25Q32_PAGE_SIZE        256U
#define W25Q32_SECTOR_SIZE      4096U
#define W25Q32_TOTAL_SIZE       0x400000U       // 4 MB

// init the CS pin (does NOT init SPI — call SPI1_Init() first)
void W25Q_Init(void);

// chip identification + reset
void W25Q_Reset(void);
uint32_t W25Q_ReadJEDECID(void);

// read — any address, any length, no alignment, no prep needed
void W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);

// write — must erase sector first, max 256 bytes per call,
// must NOT cross a 256-byte page boundary
void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len);

// erase — call before any write to that region
void W25Q_SectorErase(uint32_t addr);   // 4 KB,  ~45 ms
void W25Q_ChipErase(void);              // 4 MB,  ~25 sec

#endif /* W25Q32_DRIVER_H */
