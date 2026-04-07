/*
 * stm32f1_i2c_driver.c — I2C1 driver for STM32F103, bare metal
 *
 * PB6 = SCL, PB7 = SDA, both AF open-drain 50 MHz
 * 100 kHz standard mode
 *
 * CCR = APB1_CLK / (2 * speed) = 36MHz / 200kHz = 180
 * TRISE = (APB1 in MHz) + 1 = 37
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1_i2c_driver.h"

// PB6 = SCL, PB7 = SDA — AF open-drain, 50 MHz
// I2C needs open-drain because multiple devices share the bus
// and any device can pull the line LOW
static void i2c1_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;

    // PB6 and PB7: CNF=11 (AF open-drain), MODE=11 (50 MHz)
    GPIOB->CRL &= ~(GPIO_CRL_CNF6 | GPIO_CRL_MODE6 |
                     GPIO_CRL_CNF7 | GPIO_CRL_MODE7);
    GPIOB->CRL |= (GPIO_CRL_CNF6 | GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF7 | GPIO_CRL_MODE7);
}

void I2C1_Init(void)
{
    i2c1_gpio_init();

    // enable I2C1 clock
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // software reset — clears stuck bus state
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // APB1 freq in MHz
    I2C1->CR2 = 36;

    // CCR for 100 kHz: 36MHz / (2 * 100kHz) = 180
    I2C1->CCR = I2C_APB1_CLK / (2 * I2C_SPEED_STD);

    // max rise time: (36 MHz in us) + 1 = 37
    I2C1->TRISE = (I2C_APB1_CLK / 1000000) + 1;

    // enable peripheral
    I2C1->CR1 |= I2C_CR1_PE;
}

// wait for bus free, generate START, wait for SB flag
void I2C1_Start(void)
{
    while (I2C1->SR2 & I2C_SR2_BUSY)
        ;
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB))
        ;
}

// send 7-bit address + write bit, wait for ADDR flag
// ADDR cleared by reading SR1 then SR2 (hardware requirement)
void I2C1_WriteAddr(uint8_t addr)
{
    I2C1->DR = (addr << 1) | 0;
    while (!(I2C1->SR1 & I2C_SR1_ADDR))
        ;
    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

// wait TXE, write byte, wait BTF
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

// full transaction: START -> ADDR -> DATA -> STOP
void I2C1_WriteByte(uint8_t saddr, uint8_t data)
{
    I2C1_Start();
    I2C1_WriteAddr(saddr);
    I2C1_WriteData(data);
    I2C1_Stop();
}

// multi-byte in one transaction (one START/STOP)
void I2C1_WriteMulti(uint8_t saddr, uint8_t *data, uint8_t len)
{
    I2C1_Start();
    I2C1_WriteAddr(saddr);
    for (uint8_t i = 0; i < len; i++)
        I2C1_WriteData(data[i]);
    I2C1_Stop();
}
