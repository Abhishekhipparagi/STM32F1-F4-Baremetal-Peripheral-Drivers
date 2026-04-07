/*
 * main.c — RCC clock config test for STM32F103 Blue Pill
 * Author: Abhishek Hipparagi
 */

#include "stm32f1xx.h"
#include "stm32f1_rcc_driver.h"

static void led_init(void);
static void delay_crude(volatile uint32_t count);

volatile uint32_t actual_freq; // check in debugger

int main(void)
{
    RCC_SystemClock_Config_72MHz();

    RCC_MCO_Init(RCC_MCO_PLL_DIV2);    // PLL/2 = 36 MHz on PA8

    led_init();

    actual_freq = RCC_GetSysClockFreq(); // should be 72000000
    (void)actual_freq;

    // blink ~1 Hz — if it's way slower, still on 8 MHz
    while (1) {
        GPIOC->ODR ^= GPIO_ODR_ODR13;
        delay_crude(7200000);   // ~1s at 72 MHz
    }
}

// PC13 onboard LED, active low
// CRH: MODE13=10 (2MHz out), CNF13=00 (push-pull)
static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
}

// crude loop delay, ~10 cycles/iter at 72 MHz
// not for production, use SysTick
static void delay_crude(volatile uint32_t count)
{
    while (count--)
        ;
}
