/*
 * stm32f4_i2c_driver.h — I2C1 driver for STM32F446RE, bare metal
 *
 * PB8 = SCL (AF4), PB9 = SDA (AF4)
 * 100 kHz standard mode, APB1 = 45 MHz
 *
 * I2C registers (CR1, CR2, CCR, DR, SR1, SR2) are identical to F103.
 * What's different: GPIO config, CCR=225 (not 180), TRISE=46 (not 37).
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F4_I2C_DRIVER_H
#define STM32F4_I2C_DRIVER_H

#include "stm32f4xx.h"
#include <stdint.h>

#define I2C_APB1_CLK    45000000U
#define I2C_SPEED_STD   100000U

void    I2C1_Init(void);
void    I2C1_Start(void);
void    I2C1_Stop(void);
void    I2C1_WriteAddr(uint8_t addr);
void    I2C1_WriteData(uint8_t data);
void    I2C1_WriteByte(uint8_t saddr, uint8_t data);
void    I2C1_WriteMulti(uint8_t saddr, uint8_t *data, uint8_t len);

#endif /* STM32F4_I2C_DRIVER_H */
