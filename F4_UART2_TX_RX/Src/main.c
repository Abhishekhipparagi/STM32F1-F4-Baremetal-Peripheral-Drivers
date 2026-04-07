/*
 * main.c — UART2 echo test for STM32F446RE Nucleo
 *
 * On Nucleo, PA2/PA3 are connected to ST-Link virtual COM port
 * so you can just plug USB and open a terminal — no USB-TTL needed.
 * Or wire PA2->RX, PA3->TX, GND on external adapter.
 *
 * Terminal: 9600 baud, 8N1
 * Type -> echoes back, PA5 LED toggles on each byte.
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4xx.h"
#include "stm32f4_rcc_driver.h"
#include "stm32f4_uart_driver.h"

static void led_init(void);

int main(void)
{
    RCC_SystemClock_Config_180MHz();
    led_init();
    UART2_Init(9600);

    UART2_SendString("STM32F446RE UART Ready!\r\n");
    UART2_SendString("Type something, it will echo back.\r\n");

    while (1) {
        if (UART2_DataAvailable()) {
            uint8_t received = UART2_GetChar();
            UART2_SendChar(received);
            GPIOA->ODR ^= GPIO_ODR_OD5;    // PA5 LED, active HIGH

            if (received == '\r')
                UART2_SendString("\r\nYou pressed Enter!\r\n");
        }
    }
}

// PA5 Nucleo LED — active HIGH (unlike Blue Pill PC13 active low)
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER  &= ~GPIO_MODER_MODER5;
    GPIOA->MODER  |=  GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR5;
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPDR5;
}
