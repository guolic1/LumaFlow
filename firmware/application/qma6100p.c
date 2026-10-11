#include "qma6100p.h"

#include <stddef.h>

#define REG_CHIP_ID 0x00U
#define REG_DATA 0x01U
#define REG_RANGE 0x0FU
#define REG_BW 0x10U
#define REG_PM 0x11U
#define REG_INT_CFG 0x21U
#define REG_RESET 0x36U

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
    if (!bus.write(REG_RESET, 0xB6U))
    {
        return QMA6100P_BUS_ERROR;
    }
    /* The supplied preliminary manual gives no reset-completion flag. */
    bus.delay_ms(10U);
    if (!bus.write(REG_RESET, 0U))
    {
        return QMA6100P_BUS_ERROR;
    }
    bus.delay_ms(10U);

    static const struct
    {
        uint8_t reg;
        uint8_t mask;
        uint8_t value;
    } settings[] = {
        {REG_PM, 0x80U, 0U},
        {REG_RANGE, 0x0FU, 0x01U},
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
