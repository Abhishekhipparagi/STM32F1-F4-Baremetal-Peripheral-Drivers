/*
 * main.c — SPI1 + W25Q32 flash test for STM32F103 Blue Pill
 *
 * Wiring (W25Q32 module):
 *   PA5 -> CLK   PA7 -> DI    PA6 <- DO    PA4 -> /CS
 *   3V3 -> VCC, /WP, /HOLD     GND -> GND
 *
 * UART2 (PA2 TX) at 9600 baud for test output.
 *
 * Test sequence:
 *   1. read JEDEC ID, check for 0xEF4016 (W25Q32 = Winbond + NOR + 32 Mbit)
 *   2. erase sector 0
 *   3. page-program "Hello from SPI!" at addr 0
 *   4. read it back
 *   5. memcmp -> PASS / FAIL
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1xx.h"
#include <string.h>

#include "stm32f1_rcc_driver.h"
#include "stm32f1_uart_driver.h"
#include "stm32f1_spi_driver.h"
#include "w25q32_driver.h"

static void led_init(void);
static void uart_send_hex(uint32_t val);

int main(void)
{
    RCC_SystemClock_Config_72MHz();
    led_init();
    UART2_Init(9600);
    SPI1_Init(SPI_BR_DIV4);     // 72 / 4 = 18 MHz
    W25Q_Init();

    UART2_SendString("\r\nSPI + W25Q32 test\r\n");

    W25Q_Reset();
    UART2_SendString("Reset done\r\n");

    uint32_t id = W25Q_ReadJEDECID();
    UART2_SendString("JEDEC ID: ");
    uart_send_hex(id);
    UART2_SendString("\r\n");

    if (id == W25Q32_JEDEC_ID)
        UART2_SendString("W25Q32 detected\r\n");
    else
        UART2_SendString("ERROR: unknown chip\r\n");

    UART2_SendString("Erasing sector 0...\r\n");
    W25Q_SectorErase(0x000000);
    UART2_SendString("Erase done\r\n");

    uint8_t write_buf[] = "Hello from SPI!";
    UART2_SendString("Writing: ");
    UART2_SendString((char *)write_buf);
    UART2_SendString("\r\n");
    W25Q_PageProgram(0x000000, write_buf, sizeof(write_buf));
    UART2_SendString("Write done\r\n");

    uint8_t read_buf[32] = {0};
    W25Q_ReadData(0x000000, read_buf, sizeof(write_buf));
    UART2_SendString("Read back: ");
    UART2_SendString((char *)read_buf);
    UART2_SendString("\r\n");

    if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0)
        UART2_SendString("VERIFY: PASS\r\n");
    else
        UART2_SendString("VERIFY: FAIL\r\n");

    // blink to know code is running/not
    while (1) {
        GPIOC->ODR ^= GPIO_ODR_ODR13;
        for (volatile uint32_t i = 0; i < 2000000; i++)
            ;
    }
}

// PC13 onboard LED
static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
}

// little hex print helper, just for the JEDEC ID dump
static void uart_send_hex(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    UART2_SendString("0x");
    for (int i = 28; i >= 0; i -= 4)
        UART2_SendChar(hex[(val >> i) & 0xF]);
}
