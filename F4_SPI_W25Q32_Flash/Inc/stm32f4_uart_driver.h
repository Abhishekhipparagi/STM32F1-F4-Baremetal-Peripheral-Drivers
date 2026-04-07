/*
 * stm32f4_uart_driver.h — UART driver for STM32F446RE, bare metal
 *
 * USART2: PA2 = TX (AF7), PA3 = RX (AF7)
 * TX: polling, RX: interrupt + ring buffer
 * Sits on APB1 (45 MHz when SYSCLK = 180 MHz)
 *
 * USART registers (SR, DR, BRR, CR1) are identical to F103.
 * What's different: GPIO config (MODER/AFR instead of CRL/CRH),
 * GPIO clock on AHB1 instead of APB2, and explicit AF7 selection.
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F4_UART_DRIVER_H
#define STM32F4_UART_DRIVER_H

#include "stm32f4xx.h"
#include <stdint.h>

#define UART_APB1_CLK       45000000U   // 180 / 4 = 45 MHz
#define UART_RX_BUF_SIZE    128

void UART2_Init(uint32_t baudrate);
void UART2_SendChar(char c);
void UART2_SendString(const char *str);
uint8_t UART2_DataAvailable(void);
uint8_t UART2_GetChar(void);

#endif /* STM32F4_UART_DRIVER_H */
