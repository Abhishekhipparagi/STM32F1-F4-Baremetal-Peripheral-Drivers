/*
 * stm32f1_rcc_driver.h — RCC driver for STM32F103, bare metal
 *
 * Clock tree (72 MHz):
 *   HSE(8MHz) -> PLL(x9) -> SYSCLK(72MHz) -> AHB(/1) -> 72MHz
 *                                             +-> APB1(/2) -> 36MHz
 *                                             +-> APB2(/1) -> 72MHz
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F1_RCC_DRIVER_H
#define STM32F1_RCC_DRIVER_H


#include "stm32f1xx.h"
#include <stdint.h>

#define RCC_HSE_FREQ_HZ     8000000U
#define RCC_HSI_FREQ_HZ     8000000U

typedef enum {
    RCC_OK      = 0x00,
    RCC_ERROR   = 0x01,
    RCC_TIMEOUT = 0x02
} RCC_Status_t;

typedef enum {
    RCC_CLKSRC_HSI = 0x00,
    RCC_CLKSRC_HSE = 0x01,
    RCC_CLKSRC_PLL = 0x02
} RCC_ClkSrc_t;

typedef enum {
    RCC_PLLSRC_HSI_DIV2 = 0x00,    // 4 MHz in, max 64 MHz out
    RCC_PLLSRC_HSE      = 0x01     // 8 MHz in, max 72 MHz out
} RCC_PllSrc_t;

// reg value = (multiplier - 2), so 0x07 = x9
typedef enum {
    RCC_PLLMUL_2  = 0x00,
    RCC_PLLMUL_3  = 0x01,
    RCC_PLLMUL_4  = 0x02,
    RCC_PLLMUL_5  = 0x03,
    RCC_PLLMUL_6  = 0x04,
    RCC_PLLMUL_7  = 0x05,
    RCC_PLLMUL_8  = 0x06,
    RCC_PLLMUL_9  = 0x07,      // 8 x 9 = 72 MHz
    RCC_PLLMUL_10 = 0x08,
    RCC_PLLMUL_11 = 0x09,
    RCC_PLLMUL_12 = 0x0A,
    RCC_PLLMUL_13 = 0x0B,
    RCC_PLLMUL_14 = 0x0C,
    RCC_PLLMUL_15 = 0x0D,
    RCC_PLLMUL_16 = 0x0E
} RCC_PllMul_t;

typedef enum {
    RCC_AHB_DIV1   = 0x00,
    RCC_AHB_DIV2   = 0x08,
    RCC_AHB_DIV4   = 0x09,
    RCC_AHB_DIV8   = 0x0A,
    RCC_AHB_DIV16  = 0x0B,
    RCC_AHB_DIV64  = 0x0C,
    RCC_AHB_DIV128 = 0x0D,
    RCC_AHB_DIV256 = 0x0E,
    RCC_AHB_DIV512 = 0x0F
} RCC_AHBDiv_t;

// APB1 max 36 MHz — don't forget
typedef enum {
    RCC_APB_DIV1  = 0x00,
    RCC_APB_DIV2  = 0x04,
    RCC_APB_DIV4  = 0x05,
    RCC_APB_DIV8  = 0x06,
    RCC_APB_DIV16 = 0x07
} RCC_APBDiv_t;

typedef enum {
    RCC_MCO_NONE     = 0x00,
    RCC_MCO_SYSCLK   = 0x04,
    RCC_MCO_HSI      = 0x05,
    RCC_MCO_HSE      = 0x06,
    RCC_MCO_PLL_DIV2 = 0x07     // 36 MHz when running at 72
} RCC_MCOSrc_t;

typedef struct {
    RCC_ClkSrc_t  clk_source;
    RCC_PllSrc_t  pll_source;
    RCC_PllMul_t  pll_mul;
    RCC_AHBDiv_t  ahb_div;
    RCC_APBDiv_t  apb1_div;     // keep <= 36 MHz
    RCC_APBDiv_t  apb2_div;
} RCC_ClkConfig_t;

// direct 72 MHz setup
void RCC_SystemClock_Config_72MHz(void);

void RCC_SystemClock_Config_HSI(void);

// struct-based version for custom configs
RCC_Status_t RCC_Init(const RCC_ClkConfig_t *cfg);
void RCC_GetDefault72MHzConfig(RCC_ClkConfig_t *cfg);

// MCO output on PA8
void RCC_MCO_Init(RCC_MCOSrc_t source);

// read back from registers
uint32_t RCC_GetSysClockFreq(void);
uint32_t RCC_GetHCLKFreq(void);
uint32_t RCC_GetPCLK1Freq(void);
uint32_t RCC_GetPCLK2Freq(void);


#endif /* STM32F1_RCC_DRIVER_H */
