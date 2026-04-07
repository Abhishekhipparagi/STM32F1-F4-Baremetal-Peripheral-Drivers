/*
 * stm32f4_rcc_driver.h — RCC driver for STM32F446RE, bare metal
 *
 * Clock tree (180 MHz):
 *   HSE(8MHz) -> /M(4) -> 2MHz -> *N(180) -> 360MHz -> /P(2) -> SYSCLK(180MHz)
 *                                                    -> /Q(8) -> USB(45MHz)
 *   SYSCLK -> AHB(/1) -> 180MHz
 *             +-> APB1(/4) -> 45MHz
 *             +-> APB2(/2) -> 90MHz
 *
 * Needs voltage scale 1 + over-drive for 180 MHz.
 * 5 flash wait states with I-cache + D-cache + prefetch.
 *
 * Author: Abhishek Hipparagi
 */

#ifndef STM32F4_RCC_DRIVER_H
#define STM32F4_RCC_DRIVER_H

#include "stm32f4xx.h"
#include <stdint.h>

#define RCC_HSE_FREQ_HZ     8000000U    // Nucleo: 8 MHz from ST-Link MCO
#define RCC_HSI_FREQ_HZ     16000000U   // F4 HSI = 16 MHz (not 8 like F1!)

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
    RCC_PLLSRC_HSI = 0x00,
    RCC_PLLSRC_HSE = 0x01
} RCC_PllSrc_t;

/*
 * F4 PLL is way more complex than F1:
 *   VCO_in  = source / PLLM       (keep 1-2 MHz)
 *   VCO_out = VCO_in * PLLN       (keep 100-432 MHz)
 *   SYSCLK  = VCO_out / PLLP
 *   USB_CLK = VCO_out / PLLQ      (48 MHz ideal for USB)
 */
typedef struct {
    uint32_t pllm;      // VCO input divider (2-63)
    uint32_t plln;      // VCO multiplier (50-432)
    uint32_t pllp;      // SYSCLK divider (2, 4, 6, or 8)
    uint32_t pllq;      // USB/SDIO divider (2-15)
} RCC_PllConfig_t;

// AHB prescaler
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

// APB1 max 45 MHz, APB2 max 90 MHz
typedef enum {
    RCC_APB_DIV1  = 0x00,
    RCC_APB_DIV2  = 0x04,
    RCC_APB_DIV4  = 0x05,
    RCC_APB_DIV8  = 0x06,
    RCC_APB_DIV16 = 0x07
} RCC_APBDiv_t;

// MCO1 source (PA8)
// F4 has a prescaler on MCO — F1 didn't
typedef enum {
    RCC_MCO1_HSI = 0x00,
    RCC_MCO1_LSE = 0x01,
    RCC_MCO1_HSE = 0x02,
    RCC_MCO1_PLL = 0x03
} RCC_MCO1Src_t;

typedef enum {
    RCC_MCO1_PRE_NONE = 0x00,
    RCC_MCO1_PRE_DIV2 = 0x04,
    RCC_MCO1_PRE_DIV3 = 0x05,
    RCC_MCO1_PRE_DIV4 = 0x06,
    RCC_MCO1_PRE_DIV5 = 0x07
} RCC_MCO1Pre_t;

typedef struct {
    RCC_ClkSrc_t    clk_source;
    RCC_PllSrc_t    pll_source;
    RCC_PllConfig_t pll;
    RCC_AHBDiv_t    ahb_div;
    RCC_APBDiv_t    apb1_div;   // max 45 MHz
    RCC_APBDiv_t    apb2_div;   // max 90 MHz
} RCC_ClkConfig_t;

// direct 180 MHz setup — tested on Nucleo-F446RE
void RCC_SystemClock_Config_180MHz(void);

// HSI only, 16 MHz, no PLL
void RCC_SystemClock_Config_HSI(void);

// struct-based for custom configs
RCC_Status_t RCC_Init(const RCC_ClkConfig_t *cfg);
void RCC_GetDefault180MHzConfig(RCC_ClkConfig_t *cfg);

// MCO1 on PA8
void RCC_MCO1_Init(RCC_MCO1Src_t source, RCC_MCO1Pre_t prescaler);

// read from registers
uint32_t RCC_GetSysClockFreq(void);
uint32_t RCC_GetHCLKFreq(void);
uint32_t RCC_GetPCLK1Freq(void);
uint32_t RCC_GetPCLK2Freq(void);

#endif /* STM32F4_RCC_DRIVER_H */
