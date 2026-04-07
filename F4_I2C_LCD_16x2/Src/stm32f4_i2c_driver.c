/*
 * stm32f4_i2c_driver.c — I2C1 driver for STM32F446RE, bare metal
 *
 * PB8 = SCL, PB9 = SDA, both AF4 open-drain
 * 100 kHz standard mode, APB1 = 45 MHz
 *
 * CCR = 45MHz / (2 * 100kHz) = 225
 * TRISE = 45 + 1 = 46
 *
 * I2C low-level ops (start, stop, write) are identical to F103.
 * Only GPIO and timing values change.
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4_i2c_driver.h"

// PB8=SCL, PB9=SDA — AF4, open-drain, pull-up, very high speed
// F4 GPIO: MODER/OTYPER/OSPEEDR/PUPDR/AFR (not CRL/CRH like F1)
// open-drain is critical for I2C — multiple devices share bus
static void i2c1_gpio_init(void)
{
    // GPIO on AHB1 in F4 (was APB2 in F1)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // MODER: PB8, PB9 = AF (10)
    GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
    GPIOB->MODER |=   GPIO_MODER_MODER8_1 | GPIO_MODER_MODER9_1;

    // OTYPER: open-drain — THE critical I2C setting
    GPIOB->OTYPER |= GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9;

    // very high speed
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR8 | GPIO_OSPEEDER_OSPEEDR9;

    // pull-up (internal ~40k as backup, module has 4.7k external)
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR8 | GPIO_PUPDR_PUPDR9);
    GPIOB->PUPDR |=   GPIO_PUPDR_PUPDR8_0 | GPIO_PUPDR_PUPDR9_0;

    // AF4 for I2C1 — PB8/PB9 are in AFR[1] (pins 8-15)
    // F1 needed AFIO remap for PB8/PB9, F4 just sets AF number
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFRH0 | GPIO_AFRH_AFRH1);
    GPIOB->AFR[1] |=  (4U << 0) | (4U << 4);   // AF4 for PB8, PB9
}

void I2C1_Init(void)
{
    i2c1_gpio_init();

    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // software reset
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // APB1 freq in MHz (F1 was 36, F4 is 45)
    I2C1->CR2 = 45;

    // CCR: 45MHz / (2 * 100kHz) = 225 (F1 was 180)
    I2C1->CCR = I2C_APB1_CLK / (2 * I2C_SPEED_STD);

    // TRISE: 45 + 1 = 46 (F1 was 37)
    I2C1->TRISE = (I2C_APB1_CLK / 1000000) + 1;

    I2C1->CR1 |= I2C_CR1_PE;
}


void I2C1_Start(void)
{
    while (I2C1->SR2 & I2C_SR2_BUSY)
        ;
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB))
        ;
}

void I2C1_WriteAddr(uint8_t addr)
{
    I2C1->DR = (addr << 1) | 0;
    while (!(I2C1->SR1 & I2C_SR1_ADDR))
        ;
    (void)I2C1->SR1;
    (void)I2C1->SR2;    // clear ADDR flag (hardware requirement)
}

void I2C1_WriteData(uint8_t data)
{
    while (!(I2C1->SR1 & I2C_SR1_TXE))
        ;
    I2C1->DR = data;
    while (!(I2C1->SR1 & I2C_SR1_BTF))
        ;
}

void I2C1_Stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}

void I2C1_WriteByte(uint8_t saddr, uint8_t data)
{
    I2C1_Start();
    I2C1_WriteAddr(saddr);
    I2C1_WriteData(data);
    I2C1_Stop();
}

void I2C1_WriteMulti(uint8_t saddr, uint8_t *data, uint8_t len)
{
    I2C1_Start();
    I2C1_WriteAddr(saddr);
    for (uint8_t i = 0; i < len; i++)
        I2C1_WriteData(data[i]);
    I2C1_Stop();
}
