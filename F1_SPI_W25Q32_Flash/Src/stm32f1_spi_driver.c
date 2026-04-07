/*
 * stm32f1_spi_driver.c — SPI1 master driver for STM32F103, bare metal
 *
 * Pin config:
 *   PA5 (SCK)  -> AF push-pull, 50 MHz
 *   PA7 (MOSI) -> AF push-pull, 50 MHz
 *   PA6 (MISO) -> floating input
 *
 * Why push-pull for SPI but open-drain for I2C?
 *   SPI is point-to-point — only one slave drives a wire at a time
 *   (CS selects). No bus contention -> push-pull is fine and faster.
 *   I2C shares the bus -> needs open-drain.
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1_spi_driver.h"

// PA5 SCK, PA7 MOSI as AF push-pull 50 MHz
// PA6 MISO as floating input
static void spi1_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    // clear PA5/6/7 config bits
    GPIOA->CRL &= ~(GPIO_CRL_CNF5 | GPIO_CRL_MODE5 |
                    GPIO_CRL_CNF6 | GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF7 | GPIO_CRL_MODE7);

    // PA5 SCK: CNF=10 (AF PP), MODE=11 (50 MHz)
    GPIOA->CRL |= GPIO_CRL_CNF5_1 | GPIO_CRL_MODE5;

    // PA7 MOSI: CNF=10 (AF PP), MODE=11 (50 MHz)
    GPIOA->CRL |= GPIO_CRL_CNF7_1 | GPIO_CRL_MODE7;

    // PA6 MISO: CNF=01 (floating input), MODE=00
    GPIOA->CRL |= GPIO_CRL_CNF6_0;
}

void SPI1_Init(SPI_BaudDiv_t baud_div)
{
    // SPI1 lives on APB2 — SPI2/3 would be APB1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    spi1_gpio_init();

    // start clean — defaults give CPOL=0, CPHA=0, 8-bit, MSB first
    SPI1->CR1 = 0;

    SPI1->CR1 |= SPI_CR1_MSTR;                    // master
    SPI1->CR1 |= ((uint32_t)baud_div << 3);       // BR[5:3]

    // SSM=1 -> manage NSS in software
    // SSI=1 -> pretend NSS is HIGH internally
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;

    // enable SPI last, after all config is in place
    SPI1->CR1 |= SPI_CR1_SPE;
}

// SPI is full-duplex: every TX is also an RX. To read, send a dummy
// byte (0xFF) — the clock pulses give the slave time to respond.
uint8_t SPI1_TransmitReceive(uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE))
        ;
    SPI1->DR = data;

    // wait for RXNE — RXNE means the transfer is fully done
    // and we have the byte the slave clocked back
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;
    return (uint8_t)SPI1->DR;
}
