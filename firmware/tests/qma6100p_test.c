#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qma6100p.h"

static uint8_t regs[256];
static int fail_reg;
static int fail_value;
static bool ignore_config;
static uint32_t delay_total;
static unsigned burst_reads;
static unsigned writes;
static bool reset_asserted;
static unsigned reset_count;
static unsigned nvm_reads;
static unsigned nvm_pending_reads;
static unsigned bad_state_resets;
static unsigned startup_writes;
static uint32_t test_mode_started;

static const uint8_t startup[][2] = {
    {0x11U, 0x80U}, {0x11U, 0x84U}, {0x4AU, 0x20U}, {0x56U, 0x01U}, {0x5FU, 0x80U}, {0x5FU, 0x00U},
};

static bool read_regs(uint8_t reg, uint8_t *data, uint8_t length)
{
    if ((reg == fail_reg) && (fail_value < 0))
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
    if (reg == 0x33U)
    {
        ++nvm_reads;
        if (nvm_reads <= nvm_pending_reads)
        {
            data[0] = 0U;
            return true;
        }
    }
    assert((unsigned)reg + length <= sizeof(regs));
    memcpy(data, &regs[reg], length);
    return true;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    if ((reg == fail_reg) && ((fail_value < 0) || (value == fail_value)))
    {
        return false;
    }
    ++writes;
    if (reg == 0x36U)
    {
        if (value == 0xB6U)
        {
            reset_asserted = true;
            ++reset_count;
        }
        else
        {
            assert(reset_asserted && value == 0U);
            reset_asserted = false;
            regs[0x45] = (reset_count <= bad_state_resets) ? 0U : 0xC7U;
        }
    }
    else if ((reg == 0x11U) || (reg == 0x4AU) || (reg == 0x56U) || (reg == 0x5FU))
    {
        if (startup_writes < sizeof(startup) / sizeof(startup[0]))
        {
            assert(nvm_reads > nvm_pending_reads && (regs[0x33] & 5U) == 5U);
            assert((regs[0x45] & 0xF0U) == 0xC0U);
            assert(reg == startup[startup_writes][0] && value == startup[startup_writes][1]);
            if (startup_writes == 4U)
            {
                test_mode_started = delay_total;
            }
            if (startup_writes == 5U)
            {
                assert(delay_total - test_mode_started >= 1U);
            }
            ++startup_writes;
        }
    }
    else if (reg == 0x0FU)
    {
        assert(startup_writes == sizeof(startup) / sizeof(startup[0]));
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
    regs[0x33] = 0xA5U;
    fail_reg = -1;
    fail_value = -1;
    ignore_config = false;
    delay_total = 0U;
    burst_reads = 0U;
    writes = 0U;
    reset_asserted = false;
    reset_count = 0U;
    nvm_reads = 0U;
    nvm_pending_reads = 0U;
    bad_state_resets = 0U;
    startup_writes = 0U;
    test_mode_started = 0U;
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
    assert(device.ready && !reset_asserted && delay_total >= 12U);
    assert(startup_writes == sizeof(startup) / sizeof(startup[0]));
    assert(regs[0x0F] == 0x51U && regs[0x10] == 0xE0U);
    assert(regs[0x11] == 0x84U && regs[0x21] == 0x8CU);
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
    nvm_pending_reads = 3U;
    bad_state_resets = 1U;
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    assert(device.ready && reset_count == 2U && nvm_reads == 5U);
    reset();
    nvm_pending_reads = 9U;
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    assert(device.ready && nvm_reads == 10U);
    reset();
    bad_state_resets = 2U;
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    assert(device.ready && reset_count == 3U);
    for (unsigned mask = 0U; mask <= 4U; mask += 4U)
    {
        reset();
        regs[0x33] = (uint8_t)mask;
        assert(qma6100p_init(&device, bus) == QMA6100P_CONFIG_ERROR);
        assert(!device.ready && nvm_reads == 10U && startup_writes == 0U);
        assert(delay_total < 50U);
    }
    reset();
    regs[0x33] = 1U;
    assert(qma6100p_init(&device, bus) == QMA6100P_CONFIG_ERROR);
    assert(!device.ready && nvm_reads == 10U && startup_writes == 0U);
    reset();
    bad_state_resets = 100U;
    assert(qma6100p_init(&device, bus) == QMA6100P_CONFIG_ERROR);
    assert(!device.ready && reset_count == 3U && startup_writes == 0U);

    reset();
    regs[0] = 0x42U;
    assert(qma6100p_init(&device, bus) == QMA6100P_CHIP_ID_ERROR);
    assert(!device.ready && writes == 0U);
    reset();
    ignore_config = true;
    assert(qma6100p_init(&device, bus) == QMA6100P_CONFIG_ERROR);
    assert(!device.ready);
    reset();
    const int failed_registers[] = {0x11, 0x33, 0x36, 0x45, 0x4A, 0x56, 0x5F};
    for (unsigned i = 0U; i < sizeof(failed_registers) / sizeof(failed_registers[0]); ++i)
    {
        reset();
        fail_reg = failed_registers[i];
        assert(qma6100p_init(&device, bus) == QMA6100P_BUS_ERROR);
        assert(!device.ready);
    }
    reset();
    fail_reg = 0x5F;
    fail_value = 0;
    assert(qma6100p_init(&device, bus) == QMA6100P_BUS_ERROR);
    assert(!device.ready && startup_writes == 5U);
    puts("QMA6100P tests passed");
    return 0;
}
