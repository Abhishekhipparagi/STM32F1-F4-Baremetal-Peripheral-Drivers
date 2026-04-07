/*
 * stm32f4_rcc_driver.c — RCC driver for STM32F446RE, bare metal
 *
 * F446 clock setup is more involved than F103:
 *   - PLL has 4 params (M, N, P, Q) instead of just a multiplier
 *   - needs voltage scaling via PWR peripheral
 *   - needs over-drive mode to hit 180 MHz
 *   - 5 flash wait states
 *   - GPIO clocks on AHB1 (not APB2)
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4_rcc_driver.h"

uint32_t SystemCoreClock = 16000000U; // F446 defaults to 16 MHz HSI

// prescaler decode tables
static const uint16_t s_ahb_div_table[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 2, 4, 8,
		16, 64, 128, 256, 512 };

static const uint8_t s_apb_div_table[8] = { 1, 1, 1, 1, 2, 4, 8, 16 };

// flash wait states for F446 at 2.7-3.6V:
// 0-30=0WS, 30-60=1WS, 60-90=2WS, 90-120=3WS, 120-150=4WS, 150-180=5WS
// also enables I-cache, D-cache, prefetch (F4 has both caches unlike F1)
static void set_flash_latency(uint32_t sysclk_hz) {
	uint32_t ws;

	if (sysclk_hz <= 30000000U)
		ws = FLASH_ACR_LATENCY_0WS;
	else if (sysclk_hz <= 60000000U)
		ws = FLASH_ACR_LATENCY_1WS;
	else if (sysclk_hz <= 90000000U)
		ws = FLASH_ACR_LATENCY_2WS;
	else if (sysclk_hz <= 120000000U)
		ws = FLASH_ACR_LATENCY_3WS;
	else if (sysclk_hz <= 150000000U)
		ws = FLASH_ACR_LATENCY_4WS;
	else
		ws = FLASH_ACR_LATENCY_5WS;

	FLASH->ACR = ws | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN;
}

static uint32_t calc_pll_sysclk(RCC_PllSrc_t src, const RCC_PllConfig_t *pll) {
	uint32_t input =
			(src == RCC_PLLSRC_HSE) ? RCC_HSE_FREQ_HZ : RCC_HSI_FREQ_HZ;
	uint32_t vco = (input / pll->pllm) * pll->plln;
	return vco / pll->pllp;
}

// --- 180 MHz config: HSE 8MHz, PLL M=4 N=180 P=2
void RCC_SystemClock_Config_180MHz(void) {
	// enable HSE — on Nucleo it comes from ST-Link MCO, not a crystal
	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY))
		;

	// enable PWR clock and set voltage scale 1
	// this is F4 only — F1 doesn't have voltage scaling
	// scale 1 + over-drive = up to 180 MHz
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	PWR->CR |= PWR_CR_VOS;             // VOS = 11 -> scale 1

	// 5 wait states for 180 MHz, caches + prefetch on
	FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_ICEN | FLASH_ACR_DCEN
			| FLASH_ACR_PRFTEN;

	// bus prescalers
	RCC->CFGR |= RCC_CFGR_HPRE_DIV1;      // AHB  = 180 MHz
	RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;     // APB1 = 45 MHz (max 45)
	RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;     // APB2 = 90 MHz (max 90)

	// PLL config — must be OFF before changing
	// F4 uses a separate PLLCFGR register (F1 used CFGR)
	// PLLM=4, PLLN=180, PLLP=2, PLLQ=8, source=HSE
	//
	//  8MHz / 4 = 2MHz (VCO in)
	//  2MHz * 180 = 360MHz (VCO out)
	//  360MHz / 2 = 180MHz (SYSCLK)
	//  360MHz / 8 = 45MHz (USB — not exact 48, needs PLLSAI for USB)
	RCC->CR &= ~RCC_CR_PLLON;
	while (RCC->CR & RCC_CR_PLLRDY)
		;

	RCC->PLLCFGR = 0;
	RCC->PLLCFGR |= 4U;                        // PLLM = 4
	RCC->PLLCFGR |= (180U << 6);               // PLLN = 180
	RCC->PLLCFGR |= (((2U / 2) - 1) << 16);    // PLLP = 2 -> reg value 00
	RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;    // source = HSE
	RCC->PLLCFGR |= (8U << 24);                 // PLLQ = 8

	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY))
		;

	// over-drive — required to reach 180 MHz (max without OD is 168)
	// doesn't exist on F1
	PWR->CR |= PWR_CR_ODEN;
	while (!(PWR->CSR & PWR_CSR_ODRDY))
		;

	PWR->CR |= PWR_CR_ODSWEN;
	while (!(PWR->CSR & PWR_CSR_ODSWRDY))
		;

	// switch to PLL
	RCC->CFGR &= ~RCC_CFGR_SW;
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
		;

	SystemCoreClock = 180000000U;
}

// HSI only, 16 MHz — F4 HSI is 16 MHz not 8
void RCC_SystemClock_Config_HSI(void) {
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY))
		;

	FLASH->ACR = FLASH_ACR_LATENCY_0WS | FLASH_ACR_ICEN | FLASH_ACR_DCEN
			| FLASH_ACR_PRFTEN;
	RCC->CFGR = 0;
	SystemCoreClock = 16000000U;
}

// struct-based init for custom configs
RCC_Status_t RCC_Init(const RCC_ClkConfig_t *cfg) {
	uint32_t target_sysclk;

	// drop to HSI
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY))
		;

	RCC->CFGR &= ~RCC_CFGR_SW;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
		;

	RCC->CR &= ~RCC_CR_PLLON;
	while (RCC->CR & RCC_CR_PLLRDY)
		;

	// HSE if needed
	if (cfg->pll_source == RCC_PLLSRC_HSE
			|| cfg->clk_source == RCC_CLKSRC_HSE) {
		RCC->CR |= RCC_CR_HSEON;
		while (!(RCC->CR & RCC_CR_HSERDY))
			;
	}

	// voltage scaling + PWR clock
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	PWR->CR |= PWR_CR_VOS;

	// flash
	if (cfg->clk_source == RCC_CLKSRC_PLL)
		target_sysclk = calc_pll_sysclk(cfg->pll_source, &cfg->pll);
	else if (cfg->clk_source == RCC_CLKSRC_HSE)
		target_sysclk = RCC_HSE_FREQ_HZ;
	else
		target_sysclk = RCC_HSI_FREQ_HZ;

	set_flash_latency(target_sysclk);

	// prescalers
	{
		uint32_t cfgr = RCC->CFGR;
		cfgr &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
		cfgr |= ((uint32_t) cfg->ahb_div << 4U);
		cfgr |= ((uint32_t) cfg->apb1_div << 10U);
		cfgr |= ((uint32_t) cfg->apb2_div << 13U);
		RCC->CFGR = cfgr;
	}

	// PLL config
	if (cfg->clk_source == RCC_CLKSRC_PLL) {
		RCC->PLLCFGR = 0;
		RCC->PLLCFGR |= cfg->pll.pllm;
		RCC->PLLCFGR |= (cfg->pll.plln << 6);
		RCC->PLLCFGR |= (((cfg->pll.pllp / 2) - 1) << 16);
		RCC->PLLCFGR |= (cfg->pll.pllq << 24);

		if (cfg->pll_source == RCC_PLLSRC_HSE)
			RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;

		RCC->CR |= RCC_CR_PLLON;
		while (!(RCC->CR & RCC_CR_PLLRDY))
			;

		// over-drive if going above 168 MHz
		if (target_sysclk > 168000000U) {
			PWR->CR |= PWR_CR_ODEN;
			while (!(PWR->CSR & PWR_CSR_ODRDY))
				;
			PWR->CR |= PWR_CR_ODSWEN;
			while (!(PWR->CSR & PWR_CSR_ODSWRDY))
				;
		}
	}

	// switch sysclk
	{
		uint32_t sw, sws;

		switch (cfg->clk_source) {
		case RCC_CLKSRC_HSE:
			sw = RCC_CFGR_SW_HSE;
			sws = RCC_CFGR_SWS_HSE;
			break;
		case RCC_CLKSRC_PLL:
			sw = RCC_CFGR_SW_PLL;
			sws = RCC_CFGR_SWS_PLL;
			break;
		default:
			sw = 0;
			sws = RCC_CFGR_SWS_HSI;
			break;
		}

		RCC->CFGR &= ~RCC_CFGR_SW;
		RCC->CFGR |= sw;
		while ((RCC->CFGR & RCC_CFGR_SWS) != sws)
			;
	}

	SystemCoreClock = target_sysclk;
	return RCC_OK;
}

void RCC_GetDefault180MHzConfig(RCC_ClkConfig_t *cfg) {
	cfg->clk_source = RCC_CLKSRC_PLL;
	cfg->pll_source = RCC_PLLSRC_HSE;
	cfg->pll.pllm = 4;        // 8 / 4 = 2 MHz
	cfg->pll.plln = 180;      // 2 * 180 = 360 MHz
	cfg->pll.pllp = 2;        // 360 / 2 = 180 MHz
	cfg->pll.pllq = 8;        // 360 / 8 = 45 MHz
	cfg->ahb_div = RCC_AHB_DIV1;
	cfg->apb1_div = RCC_APB_DIV4;     // 45 MHz
	cfg->apb2_div = RCC_APB_DIV2;     // 90 MHz
}

// MCO1 on PA8 —
// F4 MCO has a prescaler,
void RCC_MCO1_Init(RCC_MCO1Src_t source, RCC_MCO1Pre_t prescaler) {
	// GPIO clocks on AHB1 in F4 (APB2 in F1)
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

	// PA8: MODER = 10 (AF), OSPEEDR = 11 (very high), AF0 for MCO1
	GPIOA->MODER &= ~GPIO_MODER_MODER8;
	GPIOA->MODER |= GPIO_MODER_MODER8_1;        // AF mode
	GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR8;     // very high speed
	GPIOA->OTYPER &= ~GPIO_OTYPER_OT8;            // push-pull
	GPIOA->AFR[1] &= ~GPIO_AFRH_AFRH0;            // AF0 = MCO1

	// select source and prescaler
	uint32_t cfgr = RCC->CFGR;
	cfgr &= ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE);
	cfgr |= ((uint32_t) source << 21);
	cfgr |= ((uint32_t) prescaler << 24);
	RCC->CFGR = cfgr;
}

// read SYSCLK from SWS bits, decode PLL M/N/P if active
uint32_t RCC_GetSysClockFreq(void) {
	uint32_t sysclk;
	uint32_t pllm, plln, pllp, pllvco;

	switch (RCC->CFGR & RCC_CFGR_SWS) {

	case RCC_CFGR_SWS_HSI:
		sysclk = 16000000U;
		break;

	case RCC_CFGR_SWS_HSE:
		sysclk = 8000000U;
		break;

	case RCC_CFGR_SWS_PLL:
		pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
		plln = (RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6;
		pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;

		if (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC)
			pllvco = (8000000U / pllm) * plln;      // HSE
		else
			pllvco = (16000000U / pllm) * plln;     // HSI

		sysclk = pllvco / pllp;
		break;

	default:
		sysclk = 16000000U;
		break;
	}

	return sysclk;
}

uint32_t RCC_GetHCLKFreq(void) {
	uint32_t idx = (RCC->CFGR & RCC_CFGR_HPRE) >> 4;
	return RCC_GetSysClockFreq() / s_ahb_div_table[idx];
}

uint32_t RCC_GetPCLK1Freq(void) {
	uint32_t idx = (RCC->CFGR & RCC_CFGR_PPRE1) >> 10;
	return RCC_GetHCLKFreq() / s_apb_div_table[idx];
}

uint32_t RCC_GetPCLK2Freq(void) {
	uint32_t idx = (RCC->CFGR & RCC_CFGR_PPRE2) >> 13;
	return RCC_GetHCLKFreq() / s_apb_div_table[idx];
}
