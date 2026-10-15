#include <nebulizer/boot_layout.h>
#include <platform/hardware.h>
#include <protocols/host/host_protocol.h>
#include <nebulizer/protocol_ids.h>

void platform_enter_boot(uint16_t frame_id)
{
    uint8_t ack[FRAME_CODEC_MAX_FRAME]; size_t length=0;
    (void)host_protocol_encode_ack(frame_id,HOST_CMD_ENTER_BOOT,true,0,ack,sizeof(ack),&length);
    /* Stop all scheduling and IRQ producers before changing output pin modes.
     * A previously queued serial frame may be truncated; Boot INFO is authoritative. */
    __disable_irq(); board_emergency_off();
    USART1->CR1 &= ~(USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE | USART_CR1_RXNEIE_RXFNEIE);
    for(size_t i=0; i<length; ++i) {
        uint32_t budget=1000000U;
        while(!(USART1->ISR & USART_ISR_TXE_TXFNF) && --budget) {}
        if(!budget) break;
        USART1->TDR=ack[i];
    }
    uint32_t budget=1000000U;
    while(!(USART1->ISR & USART_ISR_TC) && --budget) {}
    volatile uint32_t *request=(volatile uint32_t *)NB_REQUEST_ADDRESS;
    request[0]=NB_REQUEST_MAGIC; request[1]=~NB_REQUEST_MAGIC;
    __DSB(); NVIC_SystemReset();
}
