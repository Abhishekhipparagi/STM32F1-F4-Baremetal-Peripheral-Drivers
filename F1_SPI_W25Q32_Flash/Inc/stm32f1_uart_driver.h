/*
 * stm32f1_uart_driver.h — UART driver for STM32F103, bare metal
 *
 * USART2: PA2 = TX, PA3 = RX
 * TX: polling (wait for TXE, write DR, wait for TC)
 * RX: interrupt + ring buffer (ISR reads DR into circular buffer)
 *
 * Sits on APB1 bus (36 MHz when SYSCLK = 72 MHz)
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F1_UART_DRIVER_H
#define STM32F1_UART_DRIVER_H

#include "stm32f1xx.h"
#include <stdint.h>

#define UART_APB1_CLK       36000000U
#define UART_RX_BUF_SIZE    128

// init USART2: configures GPIO, BRR, enables TX+RX, sets up RX interrupt
void UART2_Init(uint32_t baudrate);

// TX — blocking/polling
void UART2_SendChar(char c);
void UART2_SendString(const char *str);

// RX — call from main loop, data comes in via interrupt
uint8_t UART2_DataAvailable(void);
uint8_t UART2_GetChar(void);

#endif /* STM32F1_UART_DRIVER_H */
