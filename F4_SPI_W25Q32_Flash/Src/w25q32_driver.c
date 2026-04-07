/*
 * w25q32_driver.c — Winbond W25Q32 SPI NOR flash driver (F446 version)
 *
 * Every command follows the same shape:
 *   CS LOW -> opcode -> [3 address bytes, MSB first] -> [data...] -> CS HIGH
 *
 * CS must stay LOW for the WHOLE command. That's why we use a manual
 * GPIO instead of hardware NSS — NSS would toggle between every byte.
 *
 * Writes are a 3-step dance:
 *   1. WriteEnable (0x06) — chip auto-clears WEL after each program/erase
 *   2. send the command + address + data
 *   3. poll status until BUSY=0
 *
 * Important: flash bits can only change 1->0. To write a sector you must
 * erase it first (sets every byte to 0xFF), then page-program over it.
 *
 * Author: Abhishek Hipparagi
 */

#include "w25q32_driver.h"
#include "stm32f4_spi_driver.h"
#include "stm32f4xx.h"

// --- W25Q32 opcodes ---
#define W25Q_WRITE_ENABLE    0x06
#define W25Q_READ_STATUS_1   0x05
#define W25Q_READ_DATA       0x03
#define W25Q_PAGE_PROGRAM    0x02
#define W25Q_SECTOR_ERASE    0x20
#define W25Q_CHIP_ERASE      0xC7
#define W25Q_JEDEC_ID        0x9F
#define W25Q_ENABLE_RESET    0x66
#define W25Q_RESET_DEVICE    0x99

// --- CS pin: PA4, manual GPIO, active LOW ---
// note F4 macro is GPIO_ODR_OD4, F1 was GPIO_ODR_ODR4 — same bit, different name
#define CS_PORT      GPIOA
#define CS_PIN_MASK  GPIO_ODR_OD4

static inline void cs_low(void)  { CS_PORT->ODR &= ~CS_PIN_MASK; }
static inline void cs_high(void) { CS_PORT->ODR |=  CS_PIN_MASK; }

// crude delay — only used after Reset (~30us required by datasheet)
// not exact but the loop runs long enough at any reasonable clock
static void crude_delay(volatile uint32_t n)
{
    while (n--)
        ;
}

void W25Q_Init(void)
{
    // PA4 as GP output, push-pull, very high speed (F4 style: 5 separate regs)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER  &= ~GPIO_MODER_MODER4;
    GPIOA->MODER  |=  GPIO_MODER_MODER4_0;       // 01 = output
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT4;           // push-pull
    GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR4;    // very high (clean CS edges)
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPDR4;         // no pull

    cs_high();   // start deselected
}

// Two-step reset (0x66 then 0x99). Two opcodes are required as a safety
// interlock — a single random 0x99 from line noise won't reset the chip.
void W25Q_Reset(void)
{
    cs_low();
    SPI1_TransmitReceive(W25Q_ENABLE_RESET);
    cs_high();

    cs_low();
    SPI1_TransmitReceive(W25Q_RESET_DEVICE);
    cs_high();

    crude_delay(200000);   // datasheet says ~30 us, give it plenty
}

// Read 3 bytes: manufacturer + memory type + capacity
// W25Q32 -> 0xEF (Winbond) + 0x40 (SPI NOR) + 0x16 (32 Mbit) = 0xEF4016
uint32_t W25Q_ReadJEDECID(void)
{
    uint32_t id = 0;

    cs_low();
    SPI1_TransmitReceive(W25Q_JEDEC_ID);
    id  = (uint32_t)SPI1_TransmitReceive(0xFF) << 16;
    id |= (uint32_t)SPI1_TransmitReceive(0xFF) << 8;
    id |= (uint32_t)SPI1_TransmitReceive(0xFF);
    cs_high();

    return id;
}

// Must be called before EVERY write/erase. Chip auto-clears WEL after.
static void w25q_write_enable(void)
{
    cs_low();
    SPI1_TransmitReceive(W25Q_WRITE_ENABLE);
    cs_high();
}

// After write/erase the chip is internally busy and ignores everything
// except Read Status. Status reg 1 bit 0 = BUSY. Spin until clear.
// Page program ~3 ms, sector erase ~45 ms, chip erase ~25 sec.
static void w25q_wait_busy(void)
{
    uint8_t status;

    cs_low();
    SPI1_TransmitReceive(W25Q_READ_STATUS_1);
    do {
        status = SPI1_TransmitReceive(0xFF);
    } while (status & 0x01);
    cs_high();
}

// Read N bytes starting at addr. No alignment, no page boundaries —
// the chip auto-increments internally so we just keep clocking dummies.
void W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len)
{
    cs_low();
    SPI1_TransmitReceive(W25Q_READ_DATA);
    SPI1_TransmitReceive((addr >> 16) & 0xFF);
    SPI1_TransmitReceive((addr >> 8)  & 0xFF);
    SPI1_TransmitReceive((addr)       & 0xFF);

    for (uint32_t i = 0; i < len; i++)
        buf[i] = SPI1_TransmitReceive(0xFF);

    cs_high();
}

// Write up to 256 bytes. Caller must ensure:
//  - sector was erased first
//  - the buffer doesn't cross a 256-byte page boundary
//    (e.g. start at 0xF0 with len 200 -> wraps inside the page, BAD)
void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (len > W25Q32_PAGE_SIZE)
        len = W25Q32_PAGE_SIZE;

    w25q_write_enable();

    cs_low();
    SPI1_TransmitReceive(W25Q_PAGE_PROGRAM);
    SPI1_TransmitReceive((addr >> 16) & 0xFF);
    SPI1_TransmitReceive((addr >> 8)  & 0xFF);
    SPI1_TransmitReceive((addr)       & 0xFF);

    for (uint32_t i = 0; i < len; i++)
        SPI1_TransmitReceive(buf[i]);

    cs_high();

    w25q_wait_busy();   // ~3 ms
}

// Erase 4 KB sector. Address gets aligned by hardware. ~45 ms.
void W25Q_SectorErase(uint32_t addr)
{
    w25q_write_enable();

    cs_low();
    SPI1_TransmitReceive(W25Q_SECTOR_ERASE);
    SPI1_TransmitReceive((addr >> 16) & 0xFF);
    SPI1_TransmitReceive((addr >> 8)  & 0xFF);
    SPI1_TransmitReceive((addr)       & 0xFF);
    cs_high();

    w25q_wait_busy();
}

// Erase the whole 4 MB chip. Slow — takes about 25 seconds!
void W25Q_ChipErase(void)
{
    w25q_write_enable();

    cs_low();
    SPI1_TransmitReceive(W25Q_CHIP_ERASE);
    cs_high();

    w25q_wait_busy();
}
