#include "qma6100p.h"

#include <stddef.h>

#define REG_CHIP_ID 0x00U
#define REG_DATA 0x01U
#define REG_MOTION_STATUS 0x09U
#define REG_RANGE 0x0FU
#define REG_BW 0x10U
#define REG_PM 0x11U
#define REG_INT_EN0 0x16U
#define REG_INT_EN1 0x17U
#define REG_INT_EN2 0x18U
#define REG_INT_MAP0 0x19U
#define REG_INT_MAP1 0x1AU
#define REG_INT_PIN 0x20U
#define REG_INT_CFG 0x21U
#define REG_MOTION_DURATION 0x2CU
#define REG_MOTION_THRESHOLD 0x2EU
#define REG_MOTION_INPUT 0x2FU
#define REG_MOTION_RESET 0x30U
#define REG_NVM 0x33U
#define REG_RESET 0x36U
#define REG_CHIP_STATE 0x45U
#define REG_TST0_ANA 0x4AU
#define REG_AFE_ANA 0x56U
#define REG_TST1_ANA 0x5FU
#define ODR_SETTLE_TIMEOUT_MS 1000U

_Static_assert(QMA6100P_RANGE_G == 8U, "Range metadata must match the 0x04 register setting");

static qma6100p_status_t startup(qma6100p_t *device)
{
    /* QST Rev. E section 6.3. Bound both waits so a bad IMU cannot block the app. */
    for (unsigned reset_attempt = 0U; reset_attempt < 3U; ++reset_attempt)
    {
        if (!device->bus.write(REG_RESET, 0xB6U))
        {
            return QMA6100P_BUS_ERROR;
        }
        device->bus.delay_ms(1U);
        if (!device->bus.write(REG_RESET, 0U))
        {
            return QMA6100P_BUS_ERROR;
        }
        bool loaded = false;
        for (unsigned poll = 0U; poll < 10U; ++poll)
        {
            uint8_t nvm;
            if (!device->bus.read(REG_NVM, &nvm, 1U))
            {
                return QMA6100P_BUS_ERROR;
            }
            if ((nvm & 0x05U) == 0x05U)
            {
                loaded = true;
                break;
            }
            device->bus.delay_ms(1U);
        }
        if (!loaded)
        {
            return QMA6100P_CONFIG_ERROR;
        }
        uint8_t state;
        if (!device->bus.read(REG_CHIP_STATE, &state, 1U))
        {
            return QMA6100P_BUS_ERROR;
        }
        if ((state & 0xF0U) == 0xC0U)
        {
            static const uint8_t sequence[][2] = {
                {REG_PM, 0x80U},      {REG_PM, 0x84U},       {REG_TST0_ANA, 0x20U},
                {REG_AFE_ANA, 0x01U}, {REG_TST1_ANA, 0x80U},
            };
            for (unsigned i = 0U; i < sizeof(sequence) / sizeof(sequence[0]); ++i)
            {
                if (!device->bus.write(sequence[i][0], sequence[i][1]))
                {
                    return QMA6100P_BUS_ERROR;
                }
            }
            device->bus.delay_ms(1U);
            return device->bus.write(REG_TST1_ANA, 0U) ? QMA6100P_OK : QMA6100P_BUS_ERROR;
        }
    }
    return QMA6100P_CONFIG_ERROR;
}

static qma6100p_status_t update(qma6100p_t *device, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t previous;
    uint8_t actual;
    if (!device->bus.read(reg, &previous, 1U) ||
        !device->bus.write(reg, (uint8_t)((previous & (uint8_t)~mask) | value)) ||
        !device->bus.read(reg, &actual, 1U))
    {
        return QMA6100P_BUS_ERROR;
    }
    return ((actual & mask) == value) ? QMA6100P_OK : QMA6100P_CONFIG_ERROR;
}

qma6100p_status_t qma6100p_init(qma6100p_t *device, qma6100p_bus_t bus)
{
    uint8_t id;
    qma6100p_status_t status;
    if (device == NULL)
    {
        return QMA6100P_CONFIG_ERROR;
    }
    device->ready = false;
    if ((bus.read == NULL) || (bus.write == NULL) || (bus.delay_ms == NULL))
    {
        return QMA6100P_CONFIG_ERROR;
    }
    device->bus = bus;
    bus.delay_ms(10U);
    if (!bus.read(REG_CHIP_ID, &id, 1U))
    {
        return QMA6100P_BUS_ERROR;
    }
    if (id != 0x90U)
    {
        return QMA6100P_CHIP_ID_ERROR;
    }
    status = startup(device);
    if (status != QMA6100P_OK)
    {
        return status;
    }

    static const struct
    {
        uint8_t reg;
        uint8_t mask;
        uint8_t value;
    } settings[] = {
        {REG_RANGE, 0x0FU, 0x04U},
        /* MCLK=51.2kHz (4), ODR=100Hz (0): see recorded RIOT clock evidence. */
        {REG_BW, 0x1FU, 0U},
        {REG_INT_CFG, 0x40U, 0U},
        {REG_PM, 0x8FU, 0x84U},
    };
    for (unsigned index = 0U; index < sizeof(settings) / sizeof(settings[0]); ++index)
    {
        status = update(device, settings[index].reg, settings[index].mask, settings[index].value);
        if (status != QMA6100P_OK)
        {
            return status;
        }
    }
    bus.delay_ms(2U);
    device->ready = true;
    return QMA6100P_OK;
}

static int16_t decode_axis(uint8_t low, uint8_t high)
{
    int32_t value = ((uint16_t)high << 6U) | (low >> 2U);
    if (value >= 0x2000)
    {
        value -= 0x4000;
    }
    return (int16_t)value;
}

qma6100p_status_t qma6100p_read(qma6100p_t *device, qma6100p_sample_t *sample)
{
    uint8_t data[6];
    if ((device == NULL) || !device->ready || (sample == NULL))
    {
        return QMA6100P_CONFIG_ERROR;
    }
    if (!device->bus.read(REG_DATA, data, sizeof(data)))
    {
        return QMA6100P_BUS_ERROR;
    }
    sample->x = decode_axis(data[0], data[1]);
    sample->y = decode_axis(data[2], data[3]);
    sample->z = decode_axis(data[4], data[5]);
    sample->new_data = (uint8_t)((data[0] & 1U) | ((data[2] & 1U) << 1U) | ((data[4] & 1U) << 2U));
    return QMA6100P_OK;
}

static qma6100p_status_t wait_fresh_samples(qma6100p_t *device, uint16_t sample_rate_hz)
{
    qma6100p_sample_t sample;
    /* Discard the old-rate NEWDATA flags. ODR changes can leave stale/full-scale
     * data for hundreds of ms, so a fixed one-period delay is insufficient. */
    qma6100p_status_t status = qma6100p_read(device, &sample);
    if (status != QMA6100P_OK)
    {
        return status;
    }
    uint32_t period_ms = 1000U / sample_rate_hz;
    unsigned fresh = 0U;
    for (uint32_t elapsed = 0U; elapsed + period_ms <= ODR_SETTLE_TIMEOUT_MS; elapsed += period_ms)
    {
        device->bus.delay_ms(period_ms);
        status = qma6100p_read(device, &sample);
        if (status != QMA6100P_OK)
        {
            return status;
        }
        fresh = sample.new_data == 7U ? fresh + 1U : 0U;
        if (fresh == 2U)
        {
            return QMA6100P_OK;
        }
    }
    return QMA6100P_CONFIG_ERROR;
}

qma6100p_status_t qma6100p_set_motion_wake(qma6100p_t *device, bool enabled, uint16_t threshold_mg,
                                           uint16_t sample_rate_hz)
{
    if (device == NULL || !device->ready)
    {
        return QMA6100P_CONFIG_ERROR;
    }
    uint32_t threshold = ((uint32_t)threshold_mg * QMA6100P_COUNTS_PER_G + 15999U) / 16000U;
    uint8_t mclk = sample_rate_hz == 25U ? 0x06U : (sample_rate_hz == 50U ? 0x05U : 0x04U);
    if (enabled && (threshold_mg == 0U || threshold > 255U ||
                    (sample_rate_hz != 25U && sample_rate_hz != 50U && sample_rate_hz != 100U)))
    {
        return QMA6100P_CONFIG_ERROR;
    }
    /* Disable sources before changing their input/ODR. Keep undefined register
     * bits intact; no other INT1 source may wake the sleeping application. */
    const struct
    {
        uint8_t reg, mask, value;
    } settings[] = {
        {REG_INT_MAP0, 0xFFU, 0U},
        {REG_INT_MAP1, 0xF3U, 0U},
        {REG_INT_EN2, 0xE7U, 0U},
        {REG_MOTION_RESET, 0x01U, 0U}, /* Hold the detector in reset across input/ODR changes. */
        {REG_INT_EN0, 0xFFU, 0U},
        {REG_INT_EN1, 0x70U, 0U},
        /* Avoid changing the live data path's 512-clock sampling divider.
         * Scale MCLK instead: 12.8/25.6/51.2kHz -> nominal 25/50/100Hz.
         * QST Rev. E 9.11/9.12; retain the CIC reset timing bits in PM. */
        {REG_BW, 0x1FU, 0U},
        {REG_PM, 0x8FU, (uint8_t)(0x80U | (enabled ? mclk : 0x04U))},
        {REG_INT_PIN, 0x03U, 0x01U},
        {REG_INT_CFG, 0xC1U, enabled ? 0x01U : 0U},
        {REG_MOTION_DURATION, 0x03U, 0U}, /* One qualifying sample. */
        {REG_MOTION_INPUT, 0x41U, 0U}, /* Adjacent-sample slope, not high-g/significant motion. */
        {REG_MOTION_RESET, 0x80U, enabled ? 0x80U : 0U}, /* Detector bypasses the data LPF. */
    };
    for (unsigned i = 0U; i < sizeof(settings) / sizeof(settings[0]); ++i)
    {
        qma6100p_status_t status =
            update(device, settings[i].reg, settings[i].mask, settings[i].value);
        if (status != QMA6100P_OK)
        {
            return status;
        }
    }
    qma6100p_status_t status = QMA6100P_OK;
    if (enabled)
    {
        status = update(device, REG_MOTION_THRESHOLD, 0xFFU, (uint8_t)threshold);
        if (status == QMA6100P_OK)
        {
            status = update(device, REG_INT_EN2, 0x07U, 0x07U);
        }
    }
    if (status == QMA6100P_OK)
    {
        status = update(device, REG_MOTION_RESET, 0x01U, 0x01U);
    }
    if (status == QMA6100P_OK)
    {
        /* Run unmapped until two fresh XYZ samples establish slope history.
         * On resume, also wait for fresh 100Hz data before publishing samples. */
        status = wait_fresh_samples(device, enabled ? sample_rate_hz : 100U);
    }
    if (status == QMA6100P_OK)
    {
        bool unused;
        status = qma6100p_read_motion(device, &unused); /* Clear initialization events last. */
    }
    if (status == QMA6100P_OK && enabled)
    {
        status = update(device, REG_INT_MAP1, 0x01U, 0x01U);
    }
    return status;
}

qma6100p_status_t qma6100p_read_motion(qma6100p_t *device, bool *motion)
{
    if (motion == NULL)
    {
        return QMA6100P_CONFIG_ERROR;
    }
    uint8_t status;
    qma6100p_status_t result = qma6100p_read_motion_status(device, &status);
    if (result == QMA6100P_OK)
    {
        *motion = (status & 0x07U) != 0U;
    }
    return result;
}

qma6100p_status_t qma6100p_read_motion_status(qma6100p_t *device, uint8_t *status)
{
    if (device == NULL || !device->ready || status == NULL)
    {
        return QMA6100P_CONFIG_ERROR;
    }
    return device->bus.read(REG_MOTION_STATUS, status, 1U) ? QMA6100P_OK : QMA6100P_BUS_ERROR;
}
