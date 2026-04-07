/*
 * stm32f1_can_driver.c — CAN1 driver for STM32F103, bare metal
 *
 * Tested at 500 kbps Normal mode against a CAN analyzer + transceiver.
 *
 * Bit timing math (500 kbps @ APB1 = 36 MHz):
 *   TQ      = (BRP) / 36 MHz = 9 / 36e6 = 250 ns
 *   bit     = (1 + TS1 + TS2) * TQ = (1 + 6 + 1) * 250 ns = 2 us
 *   rate    = 1 / 2 us = 500 kbps
 *   sample  = (1 + TS1) / (1 + TS1 + TS2) = 7/8 = 87.5%
 *
 * BTR fields are stored as (actual - 1).
 *
 * Init order matters: BTR can ONLY be written while INRQ=1 (init mode).
 * Once you call CAN1_Start() and INRQ goes back to 0, BTR is read-only.
 *
 * Author: Abhishek Hipparagi
 */

#include "stm32f1_can_driver.h"

// optional user callback — fired from the FIFO0 RX ISR
static volatile CAN_RxCallback_t s_rx_callback = 0;

// PA11 = CAN_RX (input pull-up), PA12 = CAN_TX (AF push-pull, 50 MHz)
// pull-up on RX matters: CAN bus idles RECESSIVE (logic 1), so the
// pin should read HIGH even if the transceiver is unplugged
static void can1_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;     // AFIO clock (default mapping is fine)
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;     // GPIOA clock
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;     // CAN1 clock — APB1!

    // PA11 RX: CNF=10 (input with pull), MODE=00 (input)
    GPIOA->CRH &= ~(GPIO_CRH_CNF11 | GPIO_CRH_MODE11);
    GPIOA->CRH |=  GPIO_CRH_CNF11_1;
    GPIOA->ODR |=  GPIO_ODR_ODR11;          // ODR=1 selects pull-UP

    // PA12 TX: CNF=10 (AF push-pull), MODE=11 (50 MHz)
    GPIOA->CRH &= ~(GPIO_CRH_CNF12 | GPIO_CRH_MODE12);
    GPIOA->CRH |=  GPIO_CRH_MODE12_1 | GPIO_CRH_MODE12_0;
    GPIOA->CRH |=  GPIO_CRH_CNF12_1;
}

void CAN1_GetDefault500kConfig(CAN_Config_t *cfg)
{
    cfg->prescaler = 9;             // 36 MHz / 9 = 4 MHz TQ clock (250 ns/TQ)
    cfg->ts1       = 6;             // 6 TQ
    cfg->ts2       = 1;             // 1 TQ -> 8 TQ total -> 500 kbps
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

// One filter bank, 32-bit mask mode, ID=0 / mask=0 -> matches everything.
// Without at least one ACTIVE filter, the chip silently drops every frame.
void CAN1_Filter_AcceptAll(uint8_t fifo)
{
    CAN1->FMR  |=  CAN_FMR_FINIT;       // enter filter init mode
    CAN1->FA1R &= ~CAN_FA1R_FACT0;      // deactivate filter 0 before edits

    CAN1->FS1R |=  (1U << 0);           // 32-bit scale
    CAN1->FM1R &= ~(1U << 0);           // mask mode (not list mode)

    CAN1->sFilterRegister[0].FR1 = 0x00000000;   // ID:   don't care
    CAN1->sFilterRegister[0].FR2 = 0x00000000;   // mask: all bits ignored

    if (fifo == CAN_RX_FIFO0)
        CAN1->FFA1R &= ~CAN_FFA1R_FFA0;          // route matches to FIFO0
    else
        CAN1->FFA1R |=  CAN_FFA1R_FFA0;          // or FIFO1

    CAN1->FA1R |=  CAN_FA1R_FACT0;      // activate filter 0
    CAN1->FMR  &= ~CAN_FMR_FINIT;       // leave filter init mode
}

CAN_Status_t CAN1_Start(void)
{
    // exit init mode -> CAN syncs to bus (waits for 11 recessive bits idle)
    CAN1->MCR &= ~CAN_MCR_INRQ;
    while ((CAN1->MSR & CAN_MSR_INAK) != 0) {
        CAN1->MCR &= ~CAN_MCR_INRQ;     // re-clear in case of contention
    }

    // enable FIFO0 message-pending interrupt
    CAN1->IER |= CAN_IER_FMPIE0;

    // CAN1 RX0 shares the vector with USB LP on F103.
    // Don't use USB and you get the whole interrupt to yourself.
    NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 1);
    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

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
// On F103 the CAN1 RX0 vector is shared with USB LP — that's why the
// official symbol is USB_LP_CAN_RX0_IRQHandler.
void USB_LP_CAN_RX0_IRQHandler(void)
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
