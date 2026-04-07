/*
 * main.c — RCC clock config test for STM32F446RE Nucleo
 * Author: Abhishek Hipparagi
 */

#include "stm32f4xx.h"
#include "stm32f4_rcc_driver.h"

static void led_init(void);
static void delay_crude(volatile uint32_t count);

volatile uint32_t actual_freq;

int main(void)
{
    RCC_SystemClock_Config_180MHz();

    // MCO1 on PA8: PLL / 4 = 45 MHz — check with scope
    RCC_MCO1_Init(RCC_MCO1_PLL, RCC_MCO1_PRE_DIV4);

    led_init();

    actual_freq = RCC_GetSysClockFreq(); // should be 180000000
    (void)actual_freq;

    // blink ~1 Hz
    while (1) {
        GPIOA->ODR ^= GPIO_ODR_OD5;    // PA5 LED (active high on Nucleo)
        delay_crude(18000000);          // ~1s at 180 MHz
    }
}

// PA5 onboard LED on Nucleo — active HIGH (unlike Blue Pill PC13 active low)
// F4 GPIO: MODER = 01 (output), push-pull, low speed
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;       // GPIO on AHB1, not APB2

    GPIOA->MODER  &= ~GPIO_MODER_MODER5;
    GPIOA->MODER  |=  GPIO_MODER_MODER5_0;     // 01 = output
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;         // push-pull
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR5; // low speed
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPDR5;       // no pull
}

// crude delay, ~10 cycles/iter at 180 MHz
// 1 cycle = 5.55 ns, so 18M iters ~ 1 sec
static void delay_crude(volatile uint32_t count)
{
    while (count--)
        ;
}
