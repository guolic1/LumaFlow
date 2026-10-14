#include "imu_platform.h"

#include "main.h"
#include "motion_rtc.h"

#define IMU_ADDRESS (0x12U << 1U)
#define TIMER_PERIOD_US 10000U
#define I2C_TIMEOUT_US 3000U
#define I2C_ERRORS (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)
#define I2C_CLEAR_FLAGS                                                                            \
    (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF)

static volatile uint32_t timer_ticks;
static volatile bool motion_irq;
static bool wake_enabled, sampling_enabled;
static uint32_t slept_ms;

extern void SystemClock_Config(void);

static bool wait_register(volatile uint32_t *reg, uint32_t mask)
{
    /* Bounded even when TIM3 is suspended during clock/RTC restoration. */
    for (uint32_t attempts = 0U; attempts < 1000000U; ++attempts)
    {
        if ((*reg & mask) == mask)
        {
            return true;
        }
    }
    return false;
}

static bool rtc_sync(void)
{
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
    RTC->ICSR &= ~RTC_ICSR_RSF;
    RTC->WPR = 0xFFU;
    return wait_register(&RTC->ICSR, RTC_ICSR_RSF);
}

static bool rtc_start(void)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR | LL_APB1_GRP1_PERIPH_RTC);
    LL_PWR_EnableBkUpAccess();
    if (!wait_register(&PWR->CR1, PWR_CR1_DBP))
    {
        return false;
    }
    LL_RCC_LSI_Enable();
    if (!wait_register(&RCC->CSR, RCC_CSR_LSIRDY))
    {
        return false;
    }
    uint32_t source = LL_RCC_GetRTCClockSource();
    if (source != LL_RCC_RTC_CLKSOURCE_NONE && source != LL_RCC_RTC_CLKSOURCE_LSI)
    {
        return false; /* Do not reset an independently configured backup domain. */
    }
    LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSI);
    LL_RCC_EnableRTC();
    const uint32_t prescaler = (31U << RTC_PRER_PREDIV_A_Pos) | 999U; /* Nominal LSI=32kHz. */
    if ((RTC->ICSR & RTC_ICSR_INITS) == 0U)
    {
        RTC->WPR = 0xCAU;
        RTC->WPR = 0x53U;
        RTC->ICSR |= RTC_ICSR_INIT;
        if (!wait_register(&RTC->ICSR, RTC_ICSR_INITF))
        {
            RTC->WPR = 0xFFU;
            return false;
        }
        RTC->CR = 0U; /* 24-hour shadow mode; no alarms or periodic wake interrupts. */
        RTC->PRER = 999U;
        RTC->PRER |=
            31U << RTC_PRER_PREDIV_A_Pos; /* ST sequence: synchronous, then asynchronous. */
        RTC->TR = 0U;
        RTC->DR = 0x0000C101U; /* 2000-01-01, Saturday. */
        RTC->ICSR &= ~RTC_ICSR_INIT;
        RTC->WPR = 0xFFU;
    }
    if (RTC->PRER != prescaler || RTC->CR != 0U)
    {
        return false;
    }
    return rtc_sync();
}

static uint32_t rtc_now_ms(void)
{
    /* Reading SSR locks the shadow calendar until DR is read, including midnight. */
    uint32_t subseconds = RTC->SSR;
    uint32_t time = RTC->TR;
    uint32_t date = RTC->DR;
    return motion_rtc_ms(subseconds, time, date);
}

bool imu_platform_start(void)
{
    if ((SystemCoreClock != 64000000U) || (LL_RCC_GetAPB1Prescaler() != LL_RCC_APB1_DIV_1))
    {
        return false;
    }
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM3);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM3);
    TIM3->PSC = 63U;
    TIM3->ARR = TIMER_PERIOD_US - 1U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    timer_ticks = 0U;
    slept_ms = 0U;
    sampling_enabled = true;
    TIM3->DIER = TIM_DIER_UIE;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_SetPriority(TIM3_IRQn, 2U);
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1 = TIM_CR1_CEN;
    if (!rtc_start())
    {
        imu_platform_stop();
        return false;
    }
    return true;
}

void imu_platform_stop(void)
{
    imu_platform_enable_motion_wake(false);
    NVIC_DisableIRQ(TIM3_IRQn);
    __DSB();
    __ISB();
    TIM3->CR1 = 0U;
    TIM3->DIER = 0U;
    TIM3->SR = 0U;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM3);
    sampling_enabled = false;
}

void imu_platform_set_sampling(bool enabled)
{
    if (enabled == sampling_enabled)
    {
        return;
    }
    NVIC_DisableIRQ(TIM3_IRQn);
    TIM3->CR1 = 0U;
    if ((TIM3->SR & TIM_SR_UIF) != 0U)
    {
        ++timer_ticks;
    }
    TIM3->SR = 0U;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    TIM3->DIER = enabled ? TIM_DIER_UIE : 0U;
    sampling_enabled = enabled;
    if (enabled)
    {
        NVIC_EnableIRQ(TIM3_IRQn);
        TIM3->CR1 = TIM_CR1_CEN;
    }
}

void imu_platform_enable_motion_wake(bool enabled)
{
    NVIC_DisableIRQ(EXTI0_1_IRQn);
    LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_0);
    LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_0);
    LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_0);
    NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
    motion_irq = false;
    wake_enabled = enabled;
    if (enabled)
    {
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
        LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE0);
        LL_GPIO_SetPinMode(IMU_INT_GPIO_Port, IMU_INT_Pin, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(IMU_INT_GPIO_Port, IMU_INT_Pin, LL_GPIO_PULL_NO);
        LL_EXTI_DisableFallingTrig_0_31(LL_EXTI_LINE_0);
        LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_0);
        LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_0);
        NVIC_SetPriority(EXTI0_1_IRQn, 2U);
        NVIC_EnableIRQ(EXTI0_1_IRQn);
    }
}

void EXTI0_1_IRQHandler(void)
{
    if (LL_EXTI_IsActiveRisingFlag_0_31(LL_EXTI_LINE_0))
    {
        LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_0);
        motion_irq = wake_enabled;
    }
}

bool imu_platform_motion_pending(void)
{
    /* Checking the latched pin level also covers an edge during EXTI setup. */
    return wake_enabled && (motion_irq || LL_GPIO_IsInputPinSet(IMU_INT_GPIO_Port, IMU_INT_Pin));
}

void imu_platform_clear_motion(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    motion_irq = false;
    __set_PRIMASK(primask);
}

bool imu_platform_enter_stop(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool ok = wake_enabled && !sampling_enabled;
    /* Masking interrupts closes the check-to-WFI race. A pending EXTI wakes WFI;
     * its handler runs only after the PLL and flash clock have been restored. */
    if (ok && !imu_platform_motion_pending())
    {
        uint32_t before = rtc_now_ms();
        uint32_t systick_control = SysTick->CTRL;
        SysTick->CTRL = 0U;
        LL_PWR_SetPowerMode(LL_PWR_MODE_STOP1);
        LL_LPM_EnableDeepSleep();
        __DSB();
        __WFI();
        LL_LPM_EnableSleep();
        if (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
        {
            SystemClock_Config();
        }
        /* An IRQ already pending at WFI can abort entry without stopping the PLL.
         * Never reconfigure a running PLL in that case. */
        SysTick->CTRL = systick_control;
        ok = rtc_sync();
        if (ok)
        {
            slept_ms += rtc_now_ms() - before;
        }
    }
    __set_PRIMASK(primask);
    return ok;
}

void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_UIF) != 0U)
    {
        TIM3->SR = 0U;
        ++timer_ticks;
    }
}

uint32_t imu_platform_ticks(void)
{
    return timer_ticks;
}

static uint32_t timer_now(bool milliseconds)
{
    uint32_t primask = __get_PRIMASK();
    /* Only the counter snapshot is atomic; I2C waits always allow LED/UART interrupts. */
    __disable_irq();
    uint32_t ticks = timer_ticks;
    uint32_t counter = TIM3->CNT;
    if ((TIM3->SR & TIM_SR_UIF) != 0U)
    {
        ++ticks;
        counter = TIM3->CNT;
    }
    __set_PRIMASK(primask);
    return milliseconds ? (ticks * 10U + counter / 1000U) : (ticks * TIMER_PERIOD_US + counter);
}

uint32_t imu_platform_time_ms(void)
{
    return timer_now(true) + slept_ms;
}

uint32_t imu_platform_time_us(void)
{
    return timer_now(false);
}

void imu_platform_delay_ms(uint32_t duration)
{
    uint32_t start = imu_platform_time_ms();
    while ((uint32_t)(imu_platform_time_ms() - start) < duration)
    {
    }
}

static bool wait_flag(uint32_t flag, uint32_t start)
{
    for (;;)
    {
        uint32_t status = I2C1->ISR;
        if (((status & I2C_ERRORS) != 0U) ||
            ((uint32_t)(timer_now(false) - start) >= I2C_TIMEOUT_US))
        {
            return false;
        }
        if ((status & flag) != 0U)
        {
            return true;
        }
    }
}

static bool abort_transfer(void)
{
    if ((I2C1->ISR & (I2C_ISR_BUSY | I2C_ISR_ARLO)) == I2C_ISR_BUSY)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        uint32_t start = timer_now(false);
        while (((I2C1->ISR & I2C_ISR_STOPF) == 0U) && ((uint32_t)(timer_now(false) - start) < 200U))
        {
        }
    }
    /* Reset the transfer state after errors; external lines held low remain an error. */
    LL_I2C_Disable(I2C1);
    I2C1->CR2 = 0U;
    I2C1->ICR = I2C_CLEAR_FLAGS;
    LL_I2C_Enable(I2C1);
    return false;
}

static bool begin_transfer(uint32_t start)
{
    while ((I2C1->ISR & I2C_ISR_BUSY) != 0U)
    {
        if ((uint32_t)(timer_now(false) - start) >= I2C_TIMEOUT_US)
        {
            return false;
        }
    }
    I2C1->ICR = I2C_CLEAR_FLAGS;
    return true;
}

bool imu_platform_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    uint32_t start = timer_now(false);
    if (!begin_transfer(start))
    {
        return abort_transfer();
    }
    LL_I2C_HandleTransfer(I2C1, IMU_ADDRESS, LL_I2C_ADDRSLAVE_7BIT, 1U, LL_I2C_MODE_SOFTEND,
                          LL_I2C_GENERATE_START_WRITE);
    if (!wait_flag(I2C_ISR_TXIS, start))
    {
        return abort_transfer();
    }
    LL_I2C_TransmitData8(I2C1, reg);
    if (!wait_flag(I2C_ISR_TC, start))
    {
        return abort_transfer();
    }
    LL_I2C_HandleTransfer(I2C1, IMU_ADDRESS, LL_I2C_ADDRSLAVE_7BIT, length, LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_READ);
    for (uint8_t index = 0U; index < length; ++index)
    {
        if (!wait_flag(I2C_ISR_RXNE, start))
        {
            return abort_transfer();
        }
        data[index] = LL_I2C_ReceiveData8(I2C1);
    }
    if (!wait_flag(I2C_ISR_STOPF, start))
    {
        return abort_transfer();
    }
    LL_I2C_ClearFlag_STOP(I2C1);
    return true;
}

bool imu_platform_write(uint8_t reg, uint8_t value)
{
    uint32_t start = timer_now(false);
    if (!begin_transfer(start))
    {
        return abort_transfer();
    }
    LL_I2C_HandleTransfer(I2C1, IMU_ADDRESS, LL_I2C_ADDRSLAVE_7BIT, 2U, LL_I2C_MODE_AUTOEND,
                          LL_I2C_GENERATE_START_WRITE);
    if (!wait_flag(I2C_ISR_TXIS, start))
    {
        return abort_transfer();
    }
    LL_I2C_TransmitData8(I2C1, reg);
    if (!wait_flag(I2C_ISR_TXIS, start))
    {
        return abort_transfer();
    }
    LL_I2C_TransmitData8(I2C1, value);
    if (!wait_flag(I2C_ISR_STOPF, start))
    {
        return abort_transfer();
    }
    LL_I2C_ClearFlag_STOP(I2C1);
    return true;
}
