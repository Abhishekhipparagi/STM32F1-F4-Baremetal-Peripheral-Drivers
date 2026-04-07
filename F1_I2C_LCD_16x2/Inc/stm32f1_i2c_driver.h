/*
 * stm32f1_i2c_driver.h — I2C1 driver for STM32F103, bare metal
 *
 * PB6 = SCL, PB7 = SDA (AF open-drain)
 * 100 kHz standard mode
 * APB1 = 36 MHz
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F1_I2C_DRIVER_H
#define STM32F1_I2C_DRIVER_H

#include "stm32f1xx.h"
#include <stdint.h>

#define I2C_APB1_CLK    36000000U
#define I2C_SPEED_STD   100000U     // 100 kHz

void    I2C1_Init(void);
void    I2C1_Start(void);
void    I2C1_Stop(void);
void    I2C1_WriteAddr(uint8_t addr);
void    I2C1_WriteData(uint8_t data);
void    I2C1_WriteByte(uint8_t saddr, uint8_t data);
void    I2C1_WriteMulti(uint8_t saddr, uint8_t *data, uint8_t len);

#endif /* STM32F1_I2C_DRIVER_H */
