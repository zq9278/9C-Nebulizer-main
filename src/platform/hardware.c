#include <platform/hardware.h>
#include <platform/runtime.h>
#include <platform/board_devices.h>

UART_HandleTypeDef board_host_uart, board_debug_uart, board_mist_uart;
ADC_HandleTypeDef board_adc;
I2C_HandleTypeDef board_eeprom_i2c;
TIM_HandleTypeDef board_fan_timer;
static const struct board_device *zcd_port;
static struct board_gpio_callback *zcd_callback;
static volatile uint32_t time_high;

static void gpio_init(GPIO_TypeDef *port, uint32_t pins, uint32_t mode, uint32_t pull, uint32_t af)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pins;
    gpio.Mode = mode;
    gpio.Pull = pull;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}

bool board_device_ready(const struct board_device *dev) { return dev && dev->ready; }
bool board_gpio_ready(const struct board_gpio *gpio) { return gpio && board_device_ready(gpio->port); }
int board_gpio_set(const struct board_gpio *gpio, int active)
{
    if (!board_gpio_ready(gpio)) return -ENODEV;
    bool high = !!active ^ !!(gpio->flags & GPIO_ACTIVE_LOW);
    HAL_GPIO_WritePin(gpio->port->instance, BIT(gpio->pin), high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}
int board_gpio_get(const struct board_gpio *gpio)
{
    if (!board_gpio_ready(gpio)) return -ENODEV;
    return (HAL_GPIO_ReadPin(gpio->port->instance, BIT(gpio->pin)) == GPIO_PIN_SET)
        ^ !!(gpio->flags & GPIO_ACTIVE_LOW);
}
int board_gpio_configure(const struct board_gpio *gpio, gpio_flags_t flags)
{
    if (!board_gpio_ready(gpio)) return -ENODEV;
    uint32_t key = runtime_irq_save();
    uint32_t mode = (flags & GPIO_OUTPUT) ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_INPUT;
    uint32_t combined = flags | gpio->flags;
    uint32_t pull = (combined & GPIO_PULL_UP) ? GPIO_PULLUP :
        ((combined & GPIO_PULL_DOWN) ? GPIO_PULLDOWN : GPIO_NOPULL);
    if (flags & GPIO_OUTPUT) board_gpio_set(gpio, !!(flags & BIT(2)));
    gpio_init(gpio->port->instance, BIT(gpio->pin), mode, pull, 0);
    runtime_irq_restore(key);
    return 0;
}
void board_gpio_callback_init(struct board_gpio_callback *cb,
    void (*handler)(const struct board_device *, struct board_gpio_callback *, uint32_t), uint32_t pins)
{
    cb->handler = handler;
    cb->pins = pins;
}
int board_gpio_callback_add(const struct board_device *dev, struct board_gpio_callback *cb)
{
    if (!dev || !cb || cb->pins != GPIO_PIN_2) return -EINVAL;
    zcd_port = dev;
    zcd_callback = cb;
    return 0;
}
int board_gpio_callback_remove(const struct board_device *dev, struct board_gpio_callback *cb)
{
    (void)dev;
    if (zcd_callback == cb) zcd_callback = NULL;
    return 0;
}
int board_gpio_interrupt_configure(const struct board_gpio *gpio, uint32_t flags)
{
    if (!board_gpio_ready(gpio) || gpio->pin != 2 || flags != GPIO_INT_EDGE_BOTH) return -EINVAL;
    gpio_init(gpio->port->instance, BIT(gpio->pin), GPIO_MODE_IT_RISING_FALLING, GPIO_NOPULL, 0);
    __HAL_GPIO_EXTI_CLEAR_RISING_IT(GPIO_PIN_2);
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(GPIO_PIN_2);
    HAL_NVIC_SetPriority(EXTI2_3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
    return 0;
}
void EXTI2_3_IRQHandler(void)
{
    uint32_t pending = (EXTI->RPR1 | EXTI->FPR1) & GPIO_PIN_2;
    EXTI->RPR1 = pending;
    EXTI->FPR1 = pending;
    if (pending && zcd_callback) zcd_callback->handler(zcd_port, zcd_callback, pending);
}

/* TIM15 is a continuously running 1 MHz clock. Extend its 16-bit counter. */
void TIM15_IRQHandler(void)
{
    if (TIM15->SR & TIM_SR_UIF) {
        TIM15->SR = ~TIM_SR_UIF;
        time_high += 0x10000U;
    }
}
uint32_t board_time_us(void)
{
    uint32_t key = runtime_irq_save();
    uint32_t high = time_high;
    uint32_t low = TIM15->CNT;
    if (TIM15->SR & TIM_SR_UIF) { high += 0x10000U; low = TIM15->CNT; }
    runtime_irq_restore(key);
    return high + low;
}

int board_pwm_set(const struct board_pwm *pwm, uint32_t period_ns, uint32_t pulse_ns)
{
    if (!pwm || !board_device_ready(pwm->dev) || !period_ns || pulse_ns > period_ns) return -EINVAL;
    TIM_HandleTypeDef *timer = pwm->dev->instance;
    uint32_t ticks = (uint64_t)SystemCoreClock * period_ns / 1000000000ULL;
    if (!ticks || ticks > 65535U) return -EINVAL;
    __HAL_TIM_SET_AUTORELOAD(timer, ticks - 1U);
    __HAL_TIM_SET_COMPARE(timer, pwm->channel, (uint64_t)ticks * pulse_ns / period_ns);
    return 0;
}

int board_adc_read(const struct board_adc *adc, uint16_t *value)
{
    if (!adc || !value || !board_device_ready(adc->dev)) return -EINVAL;
    ADC_HandleTypeDef *handle = adc->dev->instance;
    ADC_ChannelConfTypeDef channel = {0};
    channel.Channel = adc->channel;
    channel.Rank = ADC_RANK_CHANNEL_NUMBER;
    channel.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    HAL_StatusTypeDef status = HAL_ADC_ConfigChannel(handle, &channel);
    if (status == HAL_OK) status = HAL_ADC_Start(handle);
    if (status == HAL_OK) status = HAL_ADC_PollForConversion(handle, 2);
    if (status == HAL_OK) *value = (uint16_t)HAL_ADC_GetValue(handle);
    HAL_ADC_Stop(handle);
    channel.Rank = ADC_RANK_NONE;
    if (HAL_ADC_ConfigChannel(handle, &channel) != HAL_OK) return -EIO;
    return status == HAL_OK ? 0 : -EIO;
}
int board_i2c_write(const struct board_device *dev, const uint8_t *data, size_t len, uint16_t address)
{
    if (!board_device_ready(dev) || !data || len > UINT16_MAX) return -EINVAL;
    return HAL_I2C_Master_Transmit(dev->instance, address << 1, (uint8_t *)data, len, 25) == HAL_OK ? 0 : -EIO;
}
int board_i2c_write_read(const struct board_device *dev, uint16_t address,
    const uint8_t *prefix, size_t prefix_len, uint8_t *data, size_t len)
{
    if (!board_device_ready(dev) || !prefix || !data || (prefix_len != 1 && prefix_len != 2) || len > UINT16_MAX) return -EINVAL;
    uint16_t offset = prefix_len == 2 ? ((uint16_t)prefix[0] << 8) | prefix[1] : prefix[0];
    return HAL_I2C_Mem_Read(dev->instance, address << 1, offset,
        prefix_len == 2 ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT, data, len, 25) == HAL_OK ? 0 : -EIO;
}

void board_emergency_off(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIOB->BSRR = GPIO_PIN_1 << 16;
    GPIOB->MODER = (GPIOB->MODER & ~(3U << 2)) | (1U << 2);
    /* PA7 high-side supply is disabled by releasing the pin. */
    GPIOA->MODER &= ~(3U << 14);
    GPIOA->PUPDR &= ~(3U << 14);
    /* Fan input is inverted: drive PA6 high to stop it. */
    GPIOA->BSRR = GPIO_PIN_6;
    GPIOA->MODER = (GPIOA->MODER & ~(3U << 12)) | (1U << 12);
}

static int uart_init(UART_HandleTypeDef *uart, USART_TypeDef *instance)
{
    uart->Instance = instance;
    uart->Init.BaudRate = 115200;
    uart->Init.WordLength = UART_WORDLENGTH_8B;
    uart->Init.StopBits = UART_STOPBITS_1;
    uart->Init.Parity = UART_PARITY_NONE;
    uart->Init.Mode = UART_MODE_TX_RX;
    uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart->Init.OverSampling = UART_OVERSAMPLING_16;
    uart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    return HAL_UART_Init(uart) == HAL_OK ? 0 : -EIO;
}

int board_hardware_init(void)
{
    HAL_Init();
    board_emergency_off();
    __HAL_RCC_PWR_CLK_ENABLE();
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) return -EIO;
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSIDiv = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = RCC_PLLM_DIV1;
    osc.PLL.PLLN = 8;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return -EIO;
    RCC_ClkInitTypeDef clock = {0};
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_2) != HAL_OK) return -EIO;
    SystemCoreClockUpdate();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_USART4_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM15_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    gpio_init(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF0_USART1);
    gpio_init(GPIOA, GPIO_PIN_2 | GPIO_PIN_3, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF1_USART2);
    gpio_init(GPIOA, GPIO_PIN_0 | GPIO_PIN_1, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_AF4_USART4);
    if (uart_init(&board_host_uart, USART1) || uart_init(&board_debug_uart, USART2) || uart_init(&board_mist_uart, USART4)) return -EIO;

    TIM15->PSC = 63;
    TIM15->ARR = 0xffff;
    TIM15->EGR = TIM_EGR_UG;
    TIM15->SR = 0;
    TIM15->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM15_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM15_IRQn);
    TIM15->CR1 = TIM_CR1_CEN;

    board_fan_timer.Instance = TIM3;
    board_fan_timer.Init.Prescaler = 0;
    board_fan_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    board_fan_timer.Init.Period = 6399;
    board_fan_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&board_fan_timer) != HAL_OK) return -EIO;
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 6400; /* inactive on the inverted fan circuit */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    if (HAL_TIM_PWM_ConfigChannel(&board_fan_timer, &oc, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_Start(&board_fan_timer, TIM_CHANNEL_1) != HAL_OK) return -EIO;
    gpio_init(GPIOA, GPIO_PIN_6, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_AF1_TIM3);

    gpio_init(GPIOA, GPIO_PIN_5, GPIO_MODE_ANALOG, GPIO_NOPULL, 0);
    gpio_init(GPIOB, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_MODE_ANALOG, GPIO_NOPULL, 0);
    board_adc.Instance = ADC1;
    board_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    board_adc.Init.Resolution = ADC_RESOLUTION_12B;
    board_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    board_adc.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
    board_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    board_adc.Init.NbrOfConversion = 1;
    board_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    board_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    board_adc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    board_adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
    board_adc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;
    board_adc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;
    if (HAL_ADC_Init(&board_adc) != HAL_OK || HAL_ADCEx_Calibration_Start(&board_adc) != HAL_OK) return -EIO;

    gpio_init(GPIOB, GPIO_PIN_13 | GPIO_PIN_14, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_AF6_I2C2);
    board_eeprom_i2c.Instance = I2C2;
    /* 100 kHz at 64 MHz PCLK, analog filter on, digital filter off. */
    board_eeprom_i2c.Init.Timing = 0x10707DBC;
    board_eeprom_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    board_eeprom_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    board_eeprom_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    board_eeprom_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    board_eeprom_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&board_eeprom_i2c) != HAL_OK ||
        HAL_I2CEx_ConfigAnalogFilter(&board_eeprom_i2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&board_eeprom_i2c, 0) != HAL_OK) return -EIO;
    return 0;
}
