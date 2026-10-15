#include "update.h"
#include "stm32g0xx_hal.h"
#include <protocols/common/ring_frame_parser.h>
#include <string.h>

static UART_HandleTypeDef uart;
static struct nb_update update;
static struct ring_frame_parser parser;
static bool stay_in_boot;
static volatile bool flash_read_active, flash_read_error;
static volatile uint32_t flash_read_begin, flash_read_end;
void _init(void) {}
void _fini(void) {}

static void outputs_off(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIOB->BSRR=GPIO_PIN_1<<16;
    GPIOB->MODER=(GPIOB->MODER & ~(3U<<2)) | (1U<<2);
    /* Release high-side mist supply; stop inverted fan; no triac pulses. */
    GPIOA->MODER &= ~(3U<<14); GPIOA->PUPDR &= ~(3U<<14);
    GPIOA->BSRR=GPIO_PIN_6;
    GPIOA->MODER=(GPIOA->MODER & ~(3U<<12)) | (1U<<12);
}
void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { outputs_off(); while(1) {} }
void NMI_Handler(void)
{
    uint32_t ecc=FLASH->ECCR;
    uint32_t address=NB_FLASH_BASE+((ecc & FLASH_ECCR_ADDR_ECC)*8U);
    /* An interrupted doubleword may have invalid ECC. Only guarded App/record
     * data reads can recover; a Boot instruction fetch error is not recoverable. */
    if(flash_read_active && (ecc & FLASH_ECCR_ECCD) && !(ecc & FLASH_ECCR_SYSF_ECC) &&
        address>=flash_read_begin && address<flash_read_end) {
        flash_read_error=true;
        FLASH->ECCR |= FLASH_ECCR_ECCD | FLASH_ECCR_ECCC;
        return;
    }
    outputs_off(); while(1) {}
}

static bool flash_read(uint32_t address,uint8_t *out,size_t length)
{
    if(address<NB_APP_BASE || address>NB_FLASH_END || length>NB_FLASH_END-address) return false;
    flash_read_begin=address & ~7U; flash_read_end=(address+length+7U)&~7U;
    flash_read_error=false;
    FLASH->ECCR |= FLASH_ECCR_ECCC | FLASH_ECCR_ECCD;
    flash_read_active=true; __DSB();
    memcpy(out,(const void *)(uintptr_t)address,length);
    __DSB(); flash_read_active=false;
    if(FLASH->ECCR & FLASH_ECCR_ECCC) {
        flash_read_error=true; FLASH->ECCR |= FLASH_ECCR_ECCC;
    }
    return !flash_read_error;
}
static bool flash_erase(uint32_t address,size_t length)
{
    if(address<NB_APP_BASE || address>=NB_FLASH_END || !length ||
        length>NB_FLASH_END-address || address%NB_PAGE_SIZE || length%NB_PAGE_SIZE) return false;
    FLASH_EraseInitTypeDef erase={0}; uint32_t page_error;
    erase.TypeErase=FLASH_TYPEERASE_PAGES; erase.Banks=FLASH_BANK_1;
    erase.Page=(address-NB_FLASH_BASE)/NB_PAGE_SIZE; erase.NbPages=length/NB_PAGE_SIZE;
    if(HAL_FLASH_Unlock()!=HAL_OK) return false;
    HAL_StatusTypeDef status=HAL_FLASHEx_Erase(&erase,&page_error);
    HAL_FLASH_Lock(); return status==HAL_OK;
}
static bool flash_program(uint32_t address,const uint8_t *p,size_t length)
{
    size_t padded=(length+7U)&~7U;
    if(address<NB_APP_BASE || address>=NB_FLASH_END || (address&7U) || !length ||
        padded>NB_FLASH_END-address) return false;
    if(HAL_FLASH_Unlock()!=HAL_OK) return false;
    bool ok=true;
    for(size_t at=0; at<length; at+=8) {
        uint64_t word=UINT64_MAX; size_t n=length-at; if(n>8) n=8;
        memcpy(&word,p+at,n);
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,address+at,word)!=HAL_OK) {ok=false;break;}
    }
    HAL_FLASH_Lock(); return ok;
}
static const struct nb_flash flash={flash_read,flash_erase,flash_program};

static void received(const struct frame_codec_frame *request,void *context)
{
    (void)context;
    /* Ignore normal screen traffic; only boot protocol requests hold the boot window. */
    if(request->type!=1 || !request->payload_len ||
        request->payload[0]<NB_CMD_INFO || request->payload[0]>NB_CMD_RUN) return;
    struct frame_codec_frame reply; uint8_t bytes[FRAME_CODEC_MAX_FRAME]; size_t n;
    stay_in_boot=true;
    nb_update_handle(&update,request,&reply);
    if(frame_codec_encode(reply.frame_id,reply.type,reply.payload,reply.payload_len,
                          bytes,sizeof(bytes),&n)==0) {
        if(HAL_UART_Transmit(&uart,bytes,(uint16_t)n,100)!=HAL_OK) update.run_requested=false;
    }
}
__attribute__((naked,noreturn)) static void branch_app(uint32_t sp,uint32_t pc)
{
    (void)sp; (void)pc;
    __asm volatile("msr msp, r0\nmovs r2, #0\nmsr control, r2\nisb\ncpsie i\nbx r1");
}
static void jump_app(void)
{
    uint32_t sp=*(const uint32_t *)NB_APP_BASE, pc=*(const uint32_t *)(NB_APP_BASE+4);
    HAL_UART_DeInit(&uart); HAL_RCC_DeInit();
    __disable_irq(); SysTick->CTRL=0; SysTick->LOAD=0; SysTick->VAL=0;
    NVIC->ICER[0]=0xFFFFFFFFU; NVIC->ICPR[0]=0xFFFFFFFFU;
    SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR=NB_APP_BASE; __DSB(); __ISB(); branch_app(sp,pc);
}
int main(void)
{
    volatile uint32_t *request=(volatile uint32_t *)NB_REQUEST_ADDRESS;
    stay_in_boot=request[0]==NB_REQUEST_MAGIC && request[1]==~NB_REQUEST_MAGIC;
    request[0]=0; request[1]=0;
    HAL_Init(); outputs_off();
    if(HAL_RCC_DeInit()!=HAL_OK) while(1) {}
    SystemCoreClockUpdate();
    __HAL_FLASH_PREFETCH_BUFFER_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef gpio={0}; gpio.Pin=GPIO_PIN_6|GPIO_PIN_7;
    gpio.Mode=GPIO_MODE_AF_PP; gpio.Pull=GPIO_PULLUP;
    gpio.Speed=GPIO_SPEED_FREQ_HIGH; gpio.Alternate=GPIO_AF0_USART1;
    HAL_GPIO_Init(GPIOB,&gpio);
    uart.Instance=USART1; uart.Init.BaudRate=115200;
    uart.Init.WordLength=UART_WORDLENGTH_8B; uart.Init.StopBits=UART_STOPBITS_1;
    uart.Init.Parity=UART_PARITY_NONE; uart.Init.Mode=UART_MODE_TX_RX;
    uart.Init.HwFlowCtl=UART_HWCONTROL_NONE; uart.Init.OverSampling=UART_OVERSAMPLING_16;
    uart.Init.ClockPrescaler=UART_PRESCALER_DIV1;
    if(HAL_UART_Init(&uart)!=HAL_OK) while(1) {}
    nb_update_init(&update,&flash);
    ring_frame_parser_init(&parser,received,NULL);
    uint32_t started=HAL_GetTick(),last_byte=started;
    while(1) {
        uint8_t byte;
        if(HAL_UART_Receive(&uart,&byte,1,10)==HAL_OK) {
            last_byte=HAL_GetTick(); ring_frame_parser_feed(&parser,&byte,1);
        } else if(HAL_GetTick()-last_byte>100U) {
            /* A truncated frame cannot consume the next request indefinitely. */
            ring_frame_parser_init(&parser,received,NULL);
        }
        if(update.run_requested && update.valid) {
            /* Clean reset, then the normal verified startup window. */
            NVIC_SystemReset();
        }
        if(!stay_in_boot && update.valid && HAL_GetTick()-started>=1500U) jump_app();
    }
}
