/*
 * stm32f1_uart_driver.c — USART2 driver for STM32F103, bare metal
 *
 * TX: polling on TXE + TC flags
 * RX: interrupt-driven, stores into ring buffer
 *
 * BRR calculation:
 *   USARTDIV = f_CLK / (16 * baud)
 *   for 36MHz / (16 * 9600) = 234.375
 *   mantissa = 234, fraction = 0.375 * 16 = 6
 *   BRR = (234 << 4) | 6 = 0x0EA6
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1_uart_driver.h"

// ring buffer — ISR writes at head, main reads at tail
static volatile uint8_t  rx_buffer[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

// BRR from clock + baud, integer math (no floats on MCU)
// adds half-baud for rounding
static uint16_t compute_brr(uint32_t periph_clk, uint32_t baudrate)
{
    uint32_t divisor  = (periph_clk + (baudrate / 2U)) / baudrate;
    uint16_t mantissa = (uint16_t)(divisor / 16U);
    uint16_t fraction = (uint16_t)(divisor % 16U);
    return (mantissa << 4) | fraction;
}

// PA2 = TX (AF push-pull 50MHz), PA3 = RX (floating input)
static void uart2_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    // clear PA2 and PA3 config bits
    GPIOA->CRL &= ~(GPIO_CRL_CNF2 | GPIO_CRL_MODE2 |
                     GPIO_CRL_CNF3 | GPIO_CRL_MODE3);

    // PA2: CNF=10 (AF push-pull), MODE=11 (50MHz)
    GPIOA->CRL |= GPIO_CRL_CNF2_1 | GPIO_CRL_MODE2;

    // PA3: CNF=01 (floating input), MODE=00 (input)
    GPIOA->CRL |= GPIO_CRL_CNF3_0;
}

void UART2_Init(uint32_t baudrate)
{
    // USART2 clock — on APB1
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    uart2_gpio_init();

    // reset CR1 — BRR must be set while UE=0
    USART2->CR1 = 0;

    // baud rate
    USART2->BRR = compute_brr(UART_APB1_CLK, baudrate);

    // enable USART + TX + RX
    USART2->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    // enable RX interrupt — fires when RXNE=1 (byte received)
    USART2->CR1 |= USART_CR1_RXNEIE;

    // NVIC
    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_EnableIRQ(USART2_IRQn);
}

// TX polling: wait TXE -> write DR -> wait TC
void UART2_SendChar(char c)
{
    while (!(USART2->SR & USART_SR_TXE))
        ;
    USART2->DR = c;
    while (!(USART2->SR & USART_SR_TC))
        ;
}

void UART2_SendString(const char *str)
{
    while (*str)
        UART2_SendChar(*str++);
}

// RX interrupt handler — name must match startup vector table exactly
// reads DR (clears RXNE), stores byte in ring buffer
void USART2_IRQHandler(void)
{
    if (USART2->SR & USART_SR_RXNE) {
        uint8_t data = (uint8_t)(USART2->DR & 0xFF);
        uint16_t next_head = (rx_head + 1) % UART_RX_BUF_SIZE;

        if (next_head != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
        // if buffer full, byte is dropped — no overwrite
    }
}

// check from main loop — safe, single-producer/single-consumer
uint8_t UART2_DataAvailable(void)
{
    return (rx_head != rx_tail);
}

uint8_t UART2_GetChar(void)
{
    uint8_t data = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUF_SIZE;
    return data;
}
