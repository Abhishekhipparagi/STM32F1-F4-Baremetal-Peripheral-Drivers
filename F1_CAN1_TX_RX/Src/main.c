/*
 * main.c — CAN1 TX/RX test for STM32F103 Blue Pill
 *
 * What it does:
 *   - sends a frame with ID 0x103 every 500 ms, byte[0] = incrementing counter
 *   - on RX (any data frame), uses the first byte as a "blink count"
 *     and toggles PC13 that many times
 *
 * Hardware needed:
 *   - CAN transceiver chip (MCP2551 / SN65HVD230 / TJA1050) between
 *     PA11/PA12 and the bus — the MCU pins are TTL, the bus is differential
 *   - 120 ohm termination at BOTH ends of the bus
 *   - At least one OTHER node (analyzer or second MCU) to ACK frames,
 *     otherwise every TX fails -> error counter saturates -> bus-off
 *
 * Bus settings: 500 kbps, Normal mode, sample point 87.5%
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1xx.h"
#include "stm32f1_rcc_driver.h"
#include "stm32f1_can_driver.h"

// shared between ISR-callback and main loop
static volatile uint8_t led_blink_count   = 0;
static volatile uint8_t led_blink_pending = 0;

static void led_init(void);
static void delay_ms(uint32_t ms);
static void blink_n_times(uint8_t n);
static void on_can_rx(CAN_RxHeader_t *hdr, uint8_t *data);

int main(void)
{
    RCC_SystemClock_Config_72MHz();
    led_init();

    // configure CAN at 500 kbps Normal mode
    CAN_Config_t cfg;
    CAN1_GetDefault500kConfig(&cfg);
    CAN1_Init(&cfg);

    // accept everything into FIFO0
    CAN1_Filter_AcceptAll(CAN_RX_FIFO0);

    // hook our callback before starting — ISR is enabled in CAN1_Start
    CAN1_RegisterRxCallback(on_can_rx);

    CAN1_Start();

    // build a TX frame template — std ID 0x103, 8 data bytes
    CAN_TxHeader_t tx;
    tx.std_id = 0x103;
    tx.ide    = CAN_ID_STD;
    tx.rtr    = CAN_RTR_DATA;
    tx.dlc    = 8;

    uint8_t tx_data[8] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01 };
    uint8_t counter    = 0;
    uint32_t mailbox;

    while (1) {
        tx_data[0] = counter;
        CAN1_Transmit(&tx, tx_data, &mailbox);

        counter++;
        if (counter > 10) counter = 1;

        // process any RX from the ISR callback
        if (led_blink_pending) {
            led_blink_pending = 0;
            blink_n_times(led_blink_count);
        }

        delay_ms(500);
    }
}

// CAN RX callback — runs in ISR context, keep it short!
// just stash the work for the main loop to do
static void on_can_rx(CAN_RxHeader_t *hdr, uint8_t *data)
{
    if (hdr->rtr == CAN_RTR_DATA && hdr->dlc > 0) {
        led_blink_count   = data[0];
        led_blink_pending = 1;
    }
}

// PC13 onboard LED, active LOW
static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->ODR |= GPIO_ODR_ODR13;       // start OFF
}

// PC13 active low: ODR=0 -> ON, ODR=1 -> OFF
static void blink_n_times(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        GPIOC->ODR &= ~GPIO_ODR_ODR13;
        delay_ms(200);
        GPIOC->ODR |=  GPIO_ODR_ODR13;
        delay_ms(200);
    }
}

// SysTick-based blocking delay, calibrated off SystemCoreClock
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk))
            ;
    }
    SysTick->CTRL = 0;
}
