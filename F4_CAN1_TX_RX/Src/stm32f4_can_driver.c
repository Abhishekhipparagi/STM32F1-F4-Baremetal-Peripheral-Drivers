/*
 * stm32f4_can_driver.c — CAN1 driver for STM32F446RE, bare metal
 *
 * Tested at 500 kbps Normal mode against a CAN analyzer + transceiver,
 * pin mapping PB8 (RX) / PB9 (TX) on AF9.
 *
 * Bit timing math (500 kbps @ APB1 = 45 MHz):
 *   TQ      = (BRP) / 45 MHz = 18 / 45e6 = 400 ns
 *   bit     = (1 + TS1 + TS2) * TQ = (1 + 2 + 2) * 400 ns = 2 us
 *   rate    = 1 / 2 us = 500 kbps
 *   sample  = (1 + TS1) / (1 + TS1 + TS2) = 3/5 = 60%
 *
 * Init order matters: BTR can ONLY be written while INRQ=1 (init mode).
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f4_can_driver.h"

// optional user callback — fired from the FIFO0 RX ISR
static volatile CAN_RxCallback_t s_rx_callback = 0;

// PB8 = CAN_RX (AF9, pull-up), PB9 = CAN_TX (AF9, push-pull, very high speed)
// pull-up on RX matters: CAN bus idles RECESSIVE (logic 1), so the
// pin should read HIGH even if the transceiver is unplugged
static void can1_gpio_init(void)
{
    // GPIOB clock on AHB1 — F1 used APB2 + AFIO, F4 has none of that
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;     // CAN1 clock — APB1!

    // small read-back delay so the clock-enable settles before we
    // start poking GPIO regs (sometimes matters on F4)
    (void)RCC->APB1ENR;
    (void)RCC->AHB1ENR;

    // MODER = 10 (alternate function) for PB8 and PB9
    GPIOB->MODER &= ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9);
    GPIOB->MODER |=  (GPIO_MODER_MODER8_1 | GPIO_MODER_MODER9_1);

    // push-pull
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT8 | GPIO_OTYPER_OT9);

    // very high speed — overkill for 500 kbps but matches reference
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR8 | GPIO_OSPEEDER_OSPEEDR9;

    // PB8 RX = pull-up (PUPDR=01), PB9 TX = no pull
    // F4 has a real pull register; F1 had to abuse ODR for this
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR8 | GPIO_PUPDR_PUPDR9);
    GPIOB->PUPDR |=  GPIO_PUPDR_PUPDR8_0;   // 01 = pull-up

    // AF9 = CAN1 for PB8 and PB9. Both are >= 8 -> AFR[1] (AFRH)
    // PB8 -> bits [3:0],  PB9 -> bits [7:4]
    GPIOB->AFR[1] &= ~((0xFU << 0) | (0xFU << 4));
    GPIOB->AFR[1] |=  ((9U   << 0) | (9U   << 4));
}

void CAN1_GetDefault500kConfig(CAN_Config_t *cfg)
{
    cfg->prescaler = 18;            // 45 MHz / 18 = 2.5 MHz TQ clock (400 ns/TQ)
    cfg->ts1       = 2;             // 2 TQ
    cfg->ts2       = 2;             // 2 TQ -> 5 TQ total -> 500 kbps
    cfg->sjw       = 1;             // 1 TQ
    cfg->mode      = CAN_MODE_NORMAL;
}

CAN_Status_t CAN1_Init(const CAN_Config_t *cfg)
{
    can1_gpio_init();

    // request init mode — BTR can only be written here
    CAN1->MCR |= CAN_MCR_INRQ;
    while ((CAN1->MSR & CAN_MSR_INAK) == 0)
        ;

    // CAN starts in sleep mode after reset, wake it up
    CAN1->MCR &= ~CAN_MCR_SLEEP;
    while ((CAN1->MSR & CAN_MSR_SLAK) != 0)
        ;

    // BTR: SJW [25:24], TS2 [22:20], TS1 [19:16], BRP [9:0]
    // all stored as (actual - 1)
    CAN1->BTR = ((uint32_t)(cfg->sjw       - 1) << 24)
              | ((uint32_t)(cfg->ts2       - 1) << 20)
              | ((uint32_t)(cfg->ts1       - 1) << 16)
              | ((uint32_t)(cfg->prescaler - 1) << 0);

    // mode: LBKM (bit 30) and SILM (bit 31) in BTR
    CAN1->BTR &= ~(CAN_BTR_LBKM | CAN_BTR_SILM);
    if (cfg->mode == CAN_MODE_LOOPBACK || cfg->mode == CAN_MODE_SILENT_LOOPBACK)
        CAN1->BTR |= CAN_BTR_LBKM;
    if (cfg->mode == CAN_MODE_SILENT   || cfg->mode == CAN_MODE_SILENT_LOOPBACK)
        CAN1->BTR |= CAN_BTR_SILM;

    return CAN_OK;
}

// One filter bank in CAN1's range (0-19), 32-bit mask mode, accept all.
// F446 shares 28 filter banks between CAN1 and CAN2; CAN2SB sets the
// split point. We use 20: banks 0-19 -> CAN1, 20-27 -> CAN2.
// Without at least one ACTIVE filter, the chip silently drops every frame.
void CAN1_Filter_AcceptAll(uint8_t fifo)
{
    CAN1->FMR |= CAN_FMR_FINIT;             // enter filter init mode

    // CAN2SB = 20  (banks 0-19 belong to CAN1)
    CAN1->FMR &= ~CAN_FMR_CAN2SB_Msk;
    CAN1->FMR |= (20U << CAN_FMR_CAN2SB_Pos);

    // bank 18 — anywhere in 0..19 works, 18 matches the tested ref config
    CAN1->FA1R &= ~(1U << 18);              // deactivate before edits
    CAN1->FS1R |=  (1U << 18);              // 32-bit scale
    CAN1->FM1R &= ~(1U << 18);              // mask mode (not list mode)

    CAN1->sFilterRegister[18].FR1 = 0x00000000;  // ID:   don't care
    CAN1->sFilterRegister[18].FR2 = 0x00000000;  // mask: all bits ignored

    if (fifo == CAN_RX_FIFO0)
        CAN1->FFA1R &= ~(1U << 18);         // matches go to FIFO0
    else
        CAN1->FFA1R |=  (1U << 18);         // or FIFO1

    CAN1->FA1R |=  (1U << 18);              // activate filter 18
    CAN1->FMR  &= ~CAN_FMR_FINIT;           // leave filter init mode
}

CAN_Status_t CAN1_Start(void)
{
    // exit init mode -> CAN syncs to bus (waits for 11 recessive bits idle)
    CAN1->MCR &= ~CAN_MCR_INRQ;

    // bounded wait so a missing transceiver doesn't hang the boot
    uint32_t timeout = 5000000U;
    while ((CAN1->MSR & CAN_MSR_INAK) != 0) {
        CAN1->MCR &= ~CAN_MCR_INRQ;
        if (--timeout == 0)
            return CAN_TIMEOUT;
    }

    // enable FIFO0 message-pending interrupt
    CAN1->IER |= CAN_IER_FMPIE0;

    // CAN1 RX0 has its own dedicated vector on F4 — not shared with USB
    NVIC_SetPriority(CAN1_RX0_IRQn, 1);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);

    return CAN_OK;
}

void CAN1_RegisterRxCallback(CAN_RxCallback_t cb)
{
    s_rx_callback = cb;
}

CAN_Status_t CAN1_Transmit(const CAN_TxHeader_t *header,
                           const uint8_t *data,
                           uint32_t *mailbox)
{
    uint32_t tsr = CAN1->TSR;
    uint32_t mb;

    // need at least one empty mailbox (TME0/TME1/TME2)
    if ((tsr & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2)) == 0)
        return CAN_ERROR;

    // CODE[1:0] in TSR tells us which empty mailbox to use next
    mb = (tsr & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos;
    if (mb > 2U)
        return CAN_ERROR;

    *mailbox = (uint32_t)1 << mb;

    // TIR: standard ID at [31:21], extended ID at [31:3], IDE [2], RTR [1], TXRQ [0]
    // do NOT set TXRQ yet — that's the trigger, set it last
    if (header->ide == CAN_ID_STD) {
        CAN1->sTxMailBox[mb].TIR =
            (header->std_id << CAN_TI0R_STID_Pos) | header->rtr;
    } else {
        CAN1->sTxMailBox[mb].TIR =
            (header->ext_id << CAN_TI0R_EXID_Pos) | header->ide | header->rtr;
    }

    // DLC in low nibble of TDTR
    CAN1->sTxMailBox[mb].TDTR = header->dlc;

    // payload bytes — byte 0 goes out first on the wire
    CAN1->sTxMailBox[mb].TDLR =
        ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) |
        ((uint32_t)data[1] <<  8) | ((uint32_t)data[0]      );

    CAN1->sTxMailBox[mb].TDHR =
        ((uint32_t)data[7] << 24) | ((uint32_t)data[6] << 16) |
        ((uint32_t)data[5] <<  8) | ((uint32_t)data[4]      );

    // pull the trigger
    CAN1->sTxMailBox[mb].TIR |= CAN_TI0R_TXRQ;
    return CAN_OK;
}

CAN_Status_t CAN1_Receive(uint32_t fifo,
                          CAN_RxHeader_t *header,
                          uint8_t *data)
{
    // FMP bits = number of pending messages in this FIFO (0-3)
    if (fifo == CAN_RX_FIFO0) {
        if ((CAN1->RF0R & CAN_RF0R_FMP0) == 0)
            return CAN_ERROR;
    } else {
        if ((CAN1->RF1R & CAN_RF1R_FMP1) == 0)
            return CAN_ERROR;
    }

    // RIR: standard ID at [31:21], extended at [31:3], IDE [2], RTR [1]
    header->ide = CAN_RI0R_IDE & CAN1->sFIFOMailBox[fifo].RIR;

    if (header->ide == CAN_ID_STD) {
        header->std_id = (CAN_RI0R_STID & CAN1->sFIFOMailBox[fifo].RIR)
                         >> CAN_TI0R_STID_Pos;
    } else {
        header->ext_id = ((CAN_RI0R_EXID | CAN_RI0R_STID)
                         & CAN1->sFIFOMailBox[fifo].RIR) >> CAN_RI0R_EXID_Pos;
    }

    header->rtr = CAN_RI0R_RTR & CAN1->sFIFOMailBox[fifo].RIR;

    // DLC -- defensively cap at 8 in case of corrupt frame
    {
        uint32_t dlc_raw = (CAN_RDT0R_DLC & CAN1->sFIFOMailBox[fifo].RDTR)
                           >> CAN_RDT0R_DLC_Pos;
        header->dlc = (dlc_raw >= 8U) ? 8U : dlc_raw;
    }

    header->filter_match_index = (CAN_RDT0R_FMI & CAN1->sFIFOMailBox[fifo].RDTR)
                                 >> CAN_RDT0R_FMI_Pos;
    header->timestamp          = (CAN_RDT0R_TIME & CAN1->sFIFOMailBox[fifo].RDTR)
                                 >> CAN_RDT0R_TIME_Pos;

    // payload bytes 0-3 from RDLR, 4-7 from RDHR
    data[0] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDLR >>  0) & 0xFF);
    data[1] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDLR >>  8) & 0xFF);
    data[2] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDLR >> 16) & 0xFF);
    data[3] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDLR >> 24) & 0xFF);
    data[4] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDHR >>  0) & 0xFF);
    data[5] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDHR >>  8) & 0xFF);
    data[6] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDHR >> 16) & 0xFF);
    data[7] = (uint8_t)((CAN1->sFIFOMailBox[fifo].RDHR >> 24) & 0xFF);

    // RFOM: tells the chip we're done with this slot, advance the FIFO
    if (fifo == CAN_RX_FIFO0)
        CAN1->RF0R |= CAN_RF0R_RFOM0;
    else
        CAN1->RF1R |= CAN_RF1R_RFOM1;

    return CAN_OK;
}

// FIFO0 message-pending ISR. Name must match the startup vector table.
// On F446 the CAN1 RX0 vector is dedicated (not shared like F1's USB_LP).
void CAN1_RX0_IRQHandler(void)
{
    if ((CAN1->RF0R & CAN_RF0R_FMP0) != 0) {
        CAN_RxHeader_t hdr;
        uint8_t buf[8];

        if (CAN1_Receive(CAN_RX_FIFO0, &hdr, buf) == CAN_OK) {
            if (s_rx_callback != 0)
                s_rx_callback(&hdr, buf);
        }
    }
}
