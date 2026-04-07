/*
 * stm32f4_uart_driver.c — USART2 driver for STM32F446RE, bare metal
 *
 * TX: polling on TXE + TC
 * RX: interrupt + ring buffer
 *
 * The USART peripheral itself (SR, DR, BRR, CR1) is basically
 * identical to F103. What's different:
 *   - GPIO uses MODER/OSPEEDR/AFR instead of CRL/CRH
 *   - GPIO clock is on AHB1, not APB2
 *   - must set AF7 explicitly via AFR register (F103 was fixed mapping)
 *   - APB1 clock is 45 MHz instead of 36 MHz
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4_uart_driver.h"

// ring buffer
static volatile uint8_t  rx_buffer[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

// BRR — same formula as F103, just different clock (45 MHz vs 36 MHz)
static uint16_t compute_brr(uint32_t periph_clk, uint32_t baudrate)
{
    uint32_t divisor  = (periph_clk + (baudrate / 2U)) / baudrate;
    uint16_t mantissa = (uint16_t)(divisor / 16U);
    uint16_t fraction = (uint16_t)(divisor % 16U);
    return (mantissa << 4) | fraction;
}

// GPIO for UART2 — this is where F4 differs from F1
// PA2 = TX, PA3 = RX, both AF7 (USART2)
//
// F103: CRL register, MODE+CNF bits, fixed pin mapping
// F446: MODER + OTYPER + OSPEEDR + PUPDR + AFR, configurable AF
static void uart2_gpio_init(void)
{
    // GPIO on AHB1 in F4 (was APB2 in F1)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // MODER: PA2 and PA3 = AF (10)
    // in F4, both TX and RX go through AF mux — peripheral decides direction
    GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |=   GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1;

    // push-pull
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT2 | GPIO_OTYPER_OT3);

    // very high speed
    GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR2 | GPIO_OSPEEDER_OSPEEDR3;

    // no pull
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR2 | GPIO_PUPDR_PUPDR3);

    // AF7 for USART2 — this doesn't exist on F103 (fixed mapping there)
    // PA2 = AFR[0] bits [11:8], PA3 = AFR[0] bits [15:12]
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFRL2 | GPIO_AFRL_AFRL3);
    GPIOA->AFR[0] |=  (7U << 8) | (7U << 12);
}

void UART2_Init(uint32_t baudrate)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    uart2_gpio_init();

    USART2->CR1 = 0;
    USART2->BRR = compute_brr(UART_APB1_CLK, baudrate);
    USART2->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    USART2->CR1 |= USART_CR1_RXNEIE;

    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_EnableIRQ(USART2_IRQn);
}

// TX — same as F103, SR/DR registers are identical
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

// RX ISR — same as F103
void USART2_IRQHandler(void)
{
    if (USART2->SR & USART_SR_RXNE) {
        uint8_t data = (uint8_t)(USART2->DR & 0xFF);
        uint16_t next_head = (rx_head + 1) % UART_RX_BUF_SIZE;

        if (next_head != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
    }
}

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
