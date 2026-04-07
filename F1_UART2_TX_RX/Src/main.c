/*
 * main.c — UART2 echo test for STM32F103 Blue Pill
 *
 * Wiring: PA2(TX) -> USB-TTL RX, PA3(RX) <- USB-TTL TX, GND
 * Terminal: 9600 baud, 8N1
 * Type something -> echoes back, LED toggles on each byte
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1xx.h"
#include "stm32f1_rcc_driver.h"
#include "stm32f1_uart_driver.h"

static void led_init(void);

int main(void)
{
    RCC_SystemClock_Config_72MHz();
    led_init();
    UART2_Init(9600);

    UART2_SendString("UART Ready!\r\n");
    UART2_SendString("Type something, it will echo back.\r\n");

    while (1) {
        if (UART2_DataAvailable()) {
            uint8_t received = UART2_GetChar();
            UART2_SendChar(received);           // echo
            GPIOC->ODR ^= GPIO_ODR_ODR13;      // toggle LED

            if (received == '\r')
                UART2_SendString("\r\nYou pressed Enter!\r\n");
        }
    }
}

// PC13 onboard LED
static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
}
