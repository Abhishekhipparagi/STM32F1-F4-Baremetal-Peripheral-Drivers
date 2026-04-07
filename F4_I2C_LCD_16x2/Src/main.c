/*
 * main.c — I2C LCD test for STM32F446RE Nucleo
 *
 * Wiring: PB8(SCL)->D15, PB9(SDA)->D14 on Nucleo Arduino headers
 * PCF8574 address: 0x27 (try 0x3F if it doesn't work)
 * LCD: 16x2 with PCF8574 I2C backpack, powered from 5V
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4xx.h"
#include "stm32f4_rcc_driver.h"
#include "stm32f4_i2c_driver.h"
#include "pcf8574_lcd.h"

static void led_init(void);

// SysTick delay
void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (SystemCoreClock / 1000) - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk))
            ;
    }

    SysTick->CTRL = 0;
}

int main(void)
{
    RCC_SystemClock_Config_180MHz();
    led_init();
    I2C1_Init();
    LCD_Init();

    LCD_SetCursor(0, 0);
    LCD_SendString("Hello World!");

    LCD_SetCursor(1, 0);
    LCD_SendString("STM32F446 + I2C");

    while (1) {
        GPIOA->ODR ^= GPIO_ODR_OD5;
        delay_ms(500);
    }
}

// PA5 Nucleo LED
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER  &= ~GPIO_MODER_MODER5;
    GPIOA->MODER  |=  GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
}
