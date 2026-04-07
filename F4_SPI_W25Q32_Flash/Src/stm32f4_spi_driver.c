/*
 * stm32f4_spi_driver.c — SPI1 master driver for STM32F446RE, bare metal
 *
 * Pin config (F4-style, 5 separate GPIO regs):
 *   PA5 (SCK)  -> AF mode, AF5, push-pull, very high speed
 *   PA6 (MISO) -> AF mode, AF5, push-pull, very high speed
 *   PA7 (MOSI) -> AF mode, AF5, push-pull, very high speed
 *
 * Why push-pull and not open-drain?
 *   SPI is point-to-point — only one slave drives a wire at a time
 *   (CS selects). No bus contention -> push-pull is fine and faster.
 *   I2C shares the bus -> needs open-drain.
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4_spi_driver.h"

// PA5 SCK, PA6 MISO, PA7 MOSI -> all AF5, push-pull, very high speed
static void spi1_gpio_init(void)
{
    // GPIO clock on AHB1 (not APB2 like F1)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // MODER = 10 (alternate function) for PA5/6/7
    GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOA->MODER |=  (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);

    // push-pull
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT5 | GPIO_OTYPER_OT6 | GPIO_OTYPER_OT7);

    // very high speed — needed for clean edges at 22.5 MHz
    GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR5 |
                     GPIO_OSPEEDER_OSPEEDR6 |
                     GPIO_OSPEEDER_OSPEEDR7;

    // no pull — SPI lines are actively driven
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR5 | GPIO_PUPDR_PUPDR6 | GPIO_PUPDR_PUPDR7);

    // AF5 = SPI1 for PA5/6/7. PA5 -> AFR[0] bits [23:20], etc.
    // forget this and the pins stay at AF0 — SPI1 silently does nothing
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFRL5 | GPIO_AFRL_AFRL6 | GPIO_AFRL_AFRL7);
    GPIOA->AFR[0] |=  (5U << 20) | (5U << 24) | (5U << 28);
}

void SPI1_Init(SPI_BaudDiv_t baud_div)
{
    // SPI1 is on APB2 — same as F1. SPI2/3 would be APB1.
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    spi1_gpio_init();

    // start clean — defaults give CPOL=0, CPHA=0, 8-bit, MSB first
    SPI1->CR1 = 0;

    SPI1->CR1 |= SPI_CR1_MSTR;                     // master
    SPI1->CR1 |= ((uint32_t)baud_div << 3);        // BR[5:3]

    // SSM=1 -> manage NSS in software
    // SSI=1 -> pretend NSS is HIGH internally
    // forget SSI and the peripheral sees NSS=LOW, thinks another master
    // grabbed the bus, switches itself to slave mode -> nothing works
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;

    // enable last, after all config is in place
    SPI1->CR1 |= SPI_CR1_SPE;
}

// SPI is full-duplex: every TX is also an RX. To read, send a dummy
// byte (0xFF) — the clock pulses give the slave time to respond.
uint8_t SPI1_TransmitReceive(uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE))
        ;
    SPI1->DR = data;

    // wait on RXNE not TXE — RXNE means the byte is fully clocked
    // out and the slave's response byte is in the RX buffer
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;
    return (uint8_t)SPI1->DR;
}
