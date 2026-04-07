/*
 * stm32f1_rcc_driver.c — RCC driver for STM32F103, bare metal
 * Author: Abhishek Hipparagi
 */

#include "stm32f1_rcc_driver.h"

uint32_t SystemCoreClock = 8000000U; // HSI default

// AHB prescaler decode: HPRE[3:0] -> divider
static const uint16_t s_ahb_div_table[16] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 4, 8, 16, 64, 128, 256, 512
};

// APB prescaler decode: PPRE[2:0] -> divider
static const uint8_t s_apb_div_table[8] = {
    1, 1, 1, 1,
    2, 4, 8, 16
};

// flash wait states: 0-24MHz=0WS, 24-48=1WS, 48-72=2WS
static void set_flash_latency(uint32_t sysclk_hz)
{
    uint32_t lat;

    if (sysclk_hz <= 24000000U)
        lat = FLASH_ACR_LATENCY_0;
    else if (sysclk_hz <= 48000000U)
        lat = FLASH_ACR_LATENCY_1;
    else
        lat = FLASH_ACR_LATENCY_2;

    FLASH->ACR = lat | FLASH_ACR_PRFTBE;
}

static uint32_t calc_sysclk(const RCC_ClkConfig_t *cfg)
{
    uint32_t pll_in, mul;

    switch (cfg->clk_source) {
    case RCC_CLKSRC_HSI:  return RCC_HSI_FREQ_HZ;
    case RCC_CLKSRC_HSE:  return RCC_HSE_FREQ_HZ;
    case RCC_CLKSRC_PLL:
        pll_in = (cfg->pll_source == RCC_PLLSRC_HSE)
                 ? RCC_HSE_FREQ_HZ
                 : (RCC_HSI_FREQ_HZ / 2U);
        mul = (uint32_t)cfg->pll_mul + 2U;
        return pll_in * mul;
    default:
        return RCC_HSI_FREQ_HZ;
    }
}

// --- 72 MHz config: HSE 8MHz crystal & PLL x9 (8MHz x 9 = 72MHz) ---
void RCC_SystemClock_Config_72MHz(void)
{
    // make sure HSI is on before we touch anything
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;

    RCC->CFGR = 0x00000000U;               // reset config
    while (RCC->CFGR & RCC_CFGR_SWS)       // wait for HSI active
        ;

    // enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
        ;

    // 2 wait states for 72MHz + prefetch on
    // set this BEFORE switching to faster clock or CPU reads garbage
    FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;

    // PLL source = HSE, multiplier = x9
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;      // AHB  = 72 MHz
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;      // APB1 = 36 MHz (max 36)
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;      // APB2 = 72 MHz

    // switch to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;

    SystemCoreClock = 72000000U;
}

// HSI only, 8 MHz, no crystal needed
void RCC_SystemClock_Config_HSI(void)
{
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;

    FLASH->ACR = FLASH_ACR_LATENCY_0 | FLASH_ACR_PRFTBE;
    RCC->CFGR = 0;
    SystemCoreClock = 8000000U;
}

// configurable version using struct — same idea, parameterized
RCC_Status_t RCC_Init(const RCC_ClkConfig_t *cfg)
{
    uint32_t target_sysclk;

    // drop to HSI
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;

    RCC->CFGR = 0x00000000U;
    while (RCC->CFGR & RCC_CFGR_SWS)
        ;

    RCC->CR &= ~RCC_CR_PLLON;  // PLL off before reconfig

    if (cfg->pll_source == RCC_PLLSRC_HSE ||
        cfg->clk_source == RCC_CLKSRC_HSE) {
        RCC->CR |= RCC_CR_HSEON;
        while (!(RCC->CR & RCC_CR_HSERDY))
            ;
    }

    target_sysclk = calc_sysclk(cfg);
    set_flash_latency(target_sysclk);

    if (cfg->clk_source == RCC_CLKSRC_PLL) {
        uint32_t cfgr = RCC->CFGR;
        cfgr &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL);

        if (cfg->pll_source == RCC_PLLSRC_HSE)
            cfgr |= RCC_CFGR_PLLSRC;

        cfgr |= ((uint32_t)cfg->pll_mul << 18U);
        RCC->CFGR = cfgr;

        RCC->CR |= RCC_CR_PLLON;
        while (!(RCC->CR & RCC_CR_PLLRDY))
            ;
    }

    {
        uint32_t cfgr = RCC->CFGR;
        cfgr &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
        cfgr |= ((uint32_t)cfg->ahb_div  << 4U);
        cfgr |= ((uint32_t)cfg->apb1_div << 8U);
        cfgr |= ((uint32_t)cfg->apb2_div << 11U);
        RCC->CFGR = cfgr;
    }

    {
        uint32_t cfgr = RCC->CFGR;
        uint32_t sw, sws;

        cfgr &= ~RCC_CFGR_SW;

        switch (cfg->clk_source) {
        case RCC_CLKSRC_HSE: sw = RCC_CFGR_SW_HSE; sws = RCC_CFGR_SWS_HSE; break;
        case RCC_CLKSRC_PLL: sw = RCC_CFGR_SW_PLL; sws = RCC_CFGR_SWS_PLL; break;
        default:             sw = 0;                sws = 0;                 break;
        }

        RCC->CFGR = cfgr | sw;
        while ((RCC->CFGR & RCC_CFGR_SWS) != sws)
            ;
    }

    SystemCoreClock = target_sysclk;
    return RCC_OK;
}

void RCC_GetDefault72MHzConfig(RCC_ClkConfig_t *cfg)
{
    cfg->clk_source = RCC_CLKSRC_PLL;
    cfg->pll_source = RCC_PLLSRC_HSE;
    cfg->pll_mul    = RCC_PLLMUL_9;     // 8 x 9 = 72 MHz
    cfg->ahb_div    = RCC_AHB_DIV1;
    cfg->apb1_div   = RCC_APB_DIV2;     // 36 MHz
    cfg->apb2_div   = RCC_APB_DIV1;
}

// MCO on PA8 — for scope verification
// PA8: MODE=11 (50MHz), CNF=10 (AF push-pull)
// PLL/2 gives 36MHz when running at 72
void RCC_MCO_Init(RCC_MCOSrc_t source)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8);
    GPIOA->CRH |= (GPIO_CRH_MODE8_0 | GPIO_CRH_MODE8_1);
    GPIOA->CRH |= GPIO_CRH_CNF8_1;

    uint32_t cfgr = RCC->CFGR;
    cfgr &= ~RCC_CFGR_MCO;
    cfgr |= ((uint32_t)source << 24U);
    RCC->CFGR = cfgr;
}

// read actual SYSCLK from SWS bits
uint32_t RCC_GetSysClockFreq(void)
{
    uint32_t sysclk;
    uint32_t pllmul;
    uint32_t pllsource;

    switch (RCC->CFGR & RCC_CFGR_SWS) {

    case RCC_CFGR_SWS_HSI:
        sysclk = 8000000U;
        break;

    case RCC_CFGR_SWS_HSE:
        sysclk = 8000000U;
        break;

    case RCC_CFGR_SWS_PLL:
        pllmul = ((RCC->CFGR & RCC_CFGR_PLLMULL) >> 18) + 2;
        pllsource = RCC->CFGR & RCC_CFGR_PLLSRC;
        if (pllsource == 0)
            sysclk = (8000000U / 2) * pllmul;   // HSI/2
        else
            sysclk = 8000000U * pllmul;          // HSE
        break;

    default:
        sysclk = 8000000U;
        break;
    }

    return sysclk;
}

uint32_t RCC_GetHCLKFreq(void)
{
    uint32_t idx = (RCC->CFGR & RCC_CFGR_HPRE) >> 4U;
    return RCC_GetSysClockFreq() / s_ahb_div_table[idx];
}

uint32_t RCC_GetPCLK1Freq(void)
{
    uint32_t idx = (RCC->CFGR & RCC_CFGR_PPRE1) >> 8U;
    return RCC_GetHCLKFreq() / s_apb_div_table[idx];
}

uint32_t RCC_GetPCLK2Freq(void)
{
    uint32_t idx = (RCC->CFGR & RCC_CFGR_PPRE2) >> 11U;
    return RCC_GetHCLKFreq() / s_apb_div_table[idx];
}
