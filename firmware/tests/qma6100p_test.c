#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qma6100p.h"

static uint8_t regs[64];
static int fail_reg;
static bool ignore_config;
static uint32_t delay_total;
static unsigned burst_reads;
static unsigned writes;
static bool reset_asserted;

static bool read_regs(uint8_t reg, uint8_t *data, uint8_t length)
{
    if (reg == fail_reg)
    {
        /* A partially failed transfer must not contaminate the caller's sample. */
        data[0] = 0xFFU;
        return false;
    }
    if (reg == 0x01U)
    {
        assert(length == 6U);
        ++burst_reads;
    }
    assert((unsigned)reg + length <= sizeof(regs));
    memcpy(data, &regs[reg], length);
    return true;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    if (reg == fail_reg)
    {
        return false;
    }
    ++writes;
    if (reg == 0x36U)
    {
        if (value == 0xB6U)
        {
            reset_asserted = true;
        }
        else
        {
            assert(reset_asserted && value == 0U);
            reset_asserted = false;
        }
    }
    if (!(ignore_config && reg == 0x0FU))
    {
        regs[reg] = value;
    }
    return true;
}

static void delay_ms(uint32_t duration)
{
    delay_total += duration;
}

static qma6100p_bus_t bus = {read_regs, write_reg, delay_ms};

static void reset(void)
{
    memset(regs, 0, sizeof(regs));
    regs[0] = 0x90U;
    regs[0x0F] = 0x50U;
    regs[0x10] = 0xE7U;
    regs[0x11] = 0x60U;
    regs[0x21] = 0xCCU;
    fail_reg = -1;
    ignore_config = false;
    delay_total = 0U;
    burst_reads = 0U;
    writes = 0U;
    reset_asserted = false;
}

static void set_axis(unsigned reg, int value, unsigned flags)
{
    unsigned bits = ((unsigned)value & 0x3FFFU) << 2U;
    regs[reg] = (uint8_t)(bits | flags);
    regs[reg + 1U] = (uint8_t)(bits >> 8U);
}

int main(void)
{
    qma6100p_t device;
    qma6100p_sample_t sample;
    reset();
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    assert(device.ready && !reset_asserted && delay_total >= 20U);
    assert(regs[0x0F] == 0x51U && regs[0x10] == 0xE0U);
    assert(regs[0x11] == 0xE4U && regs[0x21] == 0x8CU);
    set_axis(1, -8192, 3);
    set_axis(3, 8191, 0);
    set_axis(5, -4096, 1);
    assert(qma6100p_read(&device, &sample) == QMA6100P_OK);
    assert(sample.x == -8192 && sample.y == 8191 && sample.z == -4096);
    assert(sample.new_data == 5U && burst_reads == 1U);
    set_axis(1, 4096, 0);
    set_axis(3, -1, 0);
    set_axis(5, 0, 0);
    assert(qma6100p_read(&device, &sample) == QMA6100P_OK);
    assert(sample.x == 4096 && sample.y == -1 && sample.z == 0 && sample.new_data == 0U);
    fail_reg = 1;
    assert(qma6100p_read(&device, &sample) == QMA6100P_BUS_ERROR);
    assert(sample.x == 4096 && sample.y == -1 && sample.z == 0);

    reset();
    regs[0] = 0x42U;
    assert(qma6100p_init(&device, bus) == QMA6100P_CHIP_ID_ERROR);
    assert(!device.ready && writes == 0U);
    reset();
    ignore_config = true;
    assert(qma6100p_init(&device, bus) == QMA6100P_CONFIG_ERROR);
    assert(!device.ready);
    reset();
    fail_reg = 0x11;
    assert(qma6100p_init(&device, bus) == QMA6100P_BUS_ERROR);
    assert(!device.ready);
    reset();
    fail_reg = 0x36;
    assert(qma6100p_init(&device, bus) == QMA6100P_BUS_ERROR);
    assert(!device.ready);
    puts("QMA6100P tests passed");
    return 0;
}
