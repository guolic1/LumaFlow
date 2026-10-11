#include "imu_platform.h"

#include "main.h"

#define IMU_ADDRESS (0x12U << 1U)
#define TIMER_PERIOD_US 10000U
#define I2C_TIMEOUT_US 3000U
#define I2C_ERRORS (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)
#define I2C_CLEAR_FLAGS                                                                            \
    (I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF)

static volatile uint32_t timer_ticks;

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
    TIM3->DIER = TIM_DIER_UIE;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_SetPriority(TIM3_IRQn, 2U);
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1 = TIM_CR1_CEN;
    return true;
}

void imu_platform_stop(void)
{
    NVIC_DisableIRQ(TIM3_IRQn);
    __DSB();
    __ISB();
    TIM3->CR1 = 0U;
    TIM3->DIER = 0U;
    TIM3->SR = 0U;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM3);
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
    return timer_now(true);
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
