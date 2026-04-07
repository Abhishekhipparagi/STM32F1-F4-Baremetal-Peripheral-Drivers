/*
 * main.c — CAN1 TX/RX test for STM32F446RE Nucleo
 *
 * What it does:
 *   - blinks LD2 three times at boot as a "board alive" sign
 *   - sends a frame with ID 0x103 every 500 ms, byte[0] = incrementing counter
 *   - on RX (any data frame), uses the first byte as a "blink count"
 *     and toggles LD2 (PA5) that many times
 *   - if CAN init/start fails, fast-blinks LD2 forever instead of hanging
 *
 * Hardware needed:
 *   - CAN transceiver chip (MCP2551 / SN65HVD230 / TJA1050) between
 *     PB8/PB9 and the bus — the MCU pins are TTL, the bus is differential
 *   - 120 ohm termination at BOTH ends of the bus
 *
 *
 * Pins on Nucleo CN5 (Arduino header):
 *   PB8 = D15 = CAN1_RX  -> transceiver RXD
 *   PB9 = D14 = CAN1_TX  -> transceiver TXD
 *
 * Bus settings: 500 kbps, Normal mode (60% sample point — matches the
 * tested HAL reference config)
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4xx.h"
#include "stm32f4_rcc_driver.h"
#include "stm32f4_can_driver.h"

// shared between ISR-callback and main loop
static volatile uint8_t led_blink_count   = 0;
static volatile uint8_t led_blink_pending = 0;

static void led_init(void);
static void delay_ms(uint32_t ms);
static void blink_n_times(uint8_t n);
static void on_can_rx(CAN_RxHeader_t *hdr, uint8_t *data);

int main(void)
{
    RCC_SystemClock_Config_180MHz();
    led_init();

    // boot heartbeat — three quick blinks so I know flashing worked
    for (int i = 0; i < 3; i++) {
        GPIOA->ODR |=  GPIO_ODR_OD5;
        delay_ms(100);
        GPIOA->ODR &= ~GPIO_ODR_OD5;
        delay_ms(100);
    }

    // configure CAN at 500 kbps Normal mode
    CAN_Config_t cfg;
    CAN1_GetDefault500kConfig(&cfg);
    CAN1_Init(&cfg);

    // accept everything into FIFO0
    CAN1_Filter_AcceptAll(CAN_RX_FIFO0);

    // hook our callback before starting — ISR is enabled in CAN1_Start
    CAN1_RegisterRxCallback(on_can_rx);

    // if start times out (no transceiver, dead bus, etc.) — fast-blink forever
    if (CAN1_Start() != CAN_OK) {
        while (1) {
            GPIOA->ODR ^= GPIO_ODR_OD5;
            delay_ms(50);
        }
    }

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

// PA5 onboard LD2 — active HIGH on Nucleo (unlike Blue Pill PC13)
// F4 GPIO: MODER=01 output, push-pull, no pull
static void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER  &= ~GPIO_MODER_MODER5;
    GPIOA->MODER  |=  GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPDR5;
    GPIOA->ODR    &= ~GPIO_ODR_OD5;     // start OFF
}

// PA5 active high: ODR=1 -> ON, ODR=0 -> OFF
static void blink_n_times(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        GPIOA->ODR |=  GPIO_ODR_OD5;
        delay_ms(200);
        GPIOA->ODR &= ~GPIO_ODR_OD5;
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
