/*
 * main.c — SPI1 + W25Q32 flash test for STM32F446RE Nucleo
 *
 * Wiring (W25Q32 module):
 *   PA5(d13) -> CLK   PA7(d11) -> DI    PA6(d12) <- DO    PA4(a1) -> /CS
 *   3V3 -> VCC, /WP, /HOLD     GND -> GND
 *
 * Status LED on PC0 (external LED + 330R), NOT PA5 — on Nucleo-F446RE
 * the onboard LD2 is wired to PA5, which is also SPI1_SCK. They can't
 * share. Either move the LED or remap SPI1 to PB3/PB4/PB5.
 *
 * UART2 over ST-Link VCP at 9600 baud — open the USB serial port and
 * you'll see the test output, no extra wires needed.
 *
 * Test sequence:
 *   1. read JEDEC ID, check for 0xEF4016
 *   2. erase sector 0
 *   3. page-program a string at addr 0
 *   4. read it back
 *   5. memcmp -> PASS / FAIL
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4xx.h"
#include <string.h>

#include "stm32f4_rcc_driver.h"
#include "stm32f4_uart_driver.h"
#include "stm32f4_spi_driver.h"
#include "w25q32_driver.h"

static void led_init(void);
static void uart_send_hex(uint32_t val);

int main(void)
{
    RCC_SystemClock_Config_180MHz();
    led_init();
    UART2_Init(9600);
    SPI1_Init(SPI_BR_DIV4);     // 90 / 4 = 22.5 MHz
    W25Q_Init();

    UART2_SendString("\r\nF446RE SPI + W25Q32 test\r\n");

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

    uint8_t write_buf[] = "Hello from F446RE SPI!";
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

    // blink PC0 to check the code is running or not
    while (1) {
        GPIOC->ODR ^= GPIO_ODR_OD0;
        for (volatile uint32_t i = 0; i < 5000000; i++)
            ;
    }
}

// PC0 status LED (external — PA5 conflicts with SPI1_SCK on Nucleo)
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    GPIOC->MODER  &= ~GPIO_MODER_MODER0;
    GPIOC->MODER  |=  GPIO_MODER_MODER0_0;     // 01 = output
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT0;         // push-pull
    GPIOC->PUPDR  &= ~GPIO_PUPDR_PUPDR0;       // no pull
}

// little hex print helper, just for the JEDEC ID dump
static void uart_send_hex(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    UART2_SendString("0x");
    for (int i = 28; i >= 0; i -= 4)
        UART2_SendChar(hex[(val >> i) & 0xF]);
}
