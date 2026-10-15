#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qma6100p.h"

static uint8_t regs[256];
static int fail_reg;
static int fail_value;
static bool ignore_config;
static int ignored_motion_reg;
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
static bool simulate_detector_start, detector_start_pending;
static uint32_t detector_released_ms;
static uint32_t data_stall_duration_ms, data_stalled_until_ms, last_data_ms;
static bool stale_data_flags, mapped_startup_event;

static void set_axis(unsigned reg, int value, unsigned flags);

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
    if (reg == 0x01U)
    {
        regs[1] &= (uint8_t)~1U;
        regs[3] &= (uint8_t)~1U;
        regs[5] &= (uint8_t)~1U;
    }
    if (reg == 0x09U)
    {
        regs[reg] = 0U; /* Selective read-to-clear for the latched motion source. */
    }
    return true;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    if ((reg == fail_reg) && ((fail_value < 0) || (value == fail_value)))
    {
        return false;
    }
    ++writes;
    if (reg == 0x10U && (value & 0x1FU) != (regs[reg] & 0x1FU))
    {
        data_stalled_until_ms = delay_total + data_stall_duration_ms;
        last_data_ms = delay_total;
        if (data_stall_duration_ms != 0U)
        {
            set_axis(1U, -8191, stale_data_flags ? 1U : 0U);
            set_axis(3U, -8191, stale_data_flags ? 1U : 0U);
            set_axis(5U, 8191, stale_data_flags ? 1U : 0U);
        }
    }
    if (simulate_detector_start && reg == 0x30U && (value & 1U) != 0U && (regs[reg] & 1U) == 0U)
    {
        detector_start_pending = true;
        detector_released_ms = delay_total;
    }
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
    if (!(ignore_config && reg == 0x0FU) && reg != ignored_motion_reg)
    {
        regs[reg] = value;
    }
    return true;
}

static void delay_ms(uint32_t duration)
{
    delay_total += duration;
    uint32_t period = (regs[0x10] & 0x1FU) == 6U ? 40U : ((regs[0x10] & 0x1FU) == 5U ? 20U : 10U);
    if (delay_total >= data_stalled_until_ms && delay_total - last_data_ms >= period)
    {
        last_data_ms = delay_total;
        set_axis(1U, -64, 1U);
        set_axis(3U, 128, 1U);
        set_axis(5U, 1365, 1U);
    }
    if (detector_start_pending && (regs[0x18] & 7U) != 0U &&
        delay_total - detector_released_ms >= period)
    {
        /* Model a first-sample transient after resetting the slope history. */
        regs[0x09] |= 1U;
        mapped_startup_event |= (regs[0x1A] & 1U) != 0U;
        detector_start_pending = false;
    }
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
    ignored_motion_reg = -1;
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
    simulate_detector_start = detector_start_pending = false;
    detector_released_ms = 0U;
    data_stall_duration_ms = data_stalled_until_ms = last_data_ms = 0U;
    stale_data_flags = mapped_startup_event = false;
}

static void set_axis(unsigned reg, int value, unsigned flags)
{
    unsigned bits = ((unsigned)value & 0x3FFFU) << 2U;
    regs[reg] = (uint8_t)(bits | flags);
    regs[reg + 1U] = (uint8_t)(bits >> 8U);
}

static void test_motion_wake(void)
{
    qma6100p_t device;
    reset();
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    regs[0x18] = 0xF8U;
    regs[0x19] = 0xFFU;
    regs[0x1A] = 0xFCU;
    regs[0x20] = 0xFEU;
    regs[0x2C] = 0xFFU;
    regs[0x2F] = 0xFFU;
    regs[0x30] = 0x7FU;
    regs[0x09] = 0x03U; /* Stale event must be cleared before arming. */
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 25U) == QMA6100P_OK);
    assert(regs[0x10] == 0xE6U && regs[0x0F] == 0x54U && regs[0x11] == 0x84U);
    assert(regs[0x18] == 0x1FU && regs[0x19] == 0U && regs[0x1A] == 0x0DU);
    assert(regs[0x20] == 0xFDU && regs[0x21] == 0x0DU);
    assert(regs[0x2C] == 0xFCU && regs[0x2E] == 128U && regs[0x2F] == 0xBEU);
    assert(regs[0x30] == 0xFFU && regs[0x09] == 0U);
    bool motion = true;
    assert(qma6100p_read_motion(&device, &motion) == QMA6100P_OK && !motion);
    regs[0x09] = 0x0CU; /* Negative Z event; the sign alone is not an event. */
    assert(qma6100p_read_motion(&device, &motion) == QMA6100P_OK && motion);
    assert(regs[0x09] == 0U);
    regs[0x09] = 0x08U;
    assert(qma6100p_read_motion(&device, &motion) == QMA6100P_OK && !motion);
    uint8_t raw_status = 0U;
    regs[0x09] = 0x8CU;
    assert(qma6100p_read_motion_status(&device, &raw_status) == QMA6100P_OK && raw_status == 0x8CU);
    assert(regs[0x09] == 0U);
    assert(qma6100p_read_motion_status(&device, NULL) == QMA6100P_CONFIG_ERROR);
    fail_reg = 0x09;
    motion = true;
    assert(qma6100p_read_motion(&device, &motion) == QMA6100P_BUS_ERROR && motion);
    fail_reg = -1;
    assert(qma6100p_set_motion_wake(&device, false, 0U, 0U) == QMA6100P_OK);
    assert(regs[0x10] == 0xE0U && (regs[0x18] & 0xE7U) == 0U);
    assert((regs[0x1A] & 0xF3U) == 0U && regs[0x21] == 0x0CU);
    assert((regs[0x30] & 1U) != 0U); /* Reset must be released in normal operation as well. */
    unsigned before = writes;
    assert(qma6100p_set_motion_wake(&device, true, 0U, 25U) == QMA6100P_CONFIG_ERROR);
    assert(qma6100p_set_motion_wake(&device, true, 4000U, 25U) == QMA6100P_CONFIG_ERROR);
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 13U) == QMA6100P_CONFIG_ERROR);
    assert(writes == before);
    fail_reg = 0x2E;
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 25U) == QMA6100P_BUS_ERROR);
    assert((regs[0x18] & 7U) == 0U); /* Failed setup never enables the detector. */
    fail_reg = -1;
    regs[0x2E] = 0U;
    ignored_motion_reg = 0x2E;
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 25U) == QMA6100P_CONFIG_ERROR);
    assert((regs[0x18] & 7U) == 0U);
}

static void test_motion_detector_startup(void)
{
    const uint16_t rates[] = {25U, 50U, 100U};
    for (unsigned i = 0U; i < sizeof(rates) / sizeof(rates[0]); ++i)
    {
        qma6100p_t device;
        reset();
        assert(qma6100p_init(&device, bus) == QMA6100P_OK);
        simulate_detector_start = true;
        assert(qma6100p_set_motion_wake(&device, true, 2000U, rates[i]) == QMA6100P_OK);
        assert(!mapped_startup_event);
        delay_ms(1000U / rates[i]); /* Stationary first sample must not wake immediately. */
        bool motion;
        assert(qma6100p_read_motion(&device, &motion) == QMA6100P_OK && !motion);
        regs[0x09] = 4U; /* A real event after arming is still latched and reported. */
        assert(qma6100p_read_motion(&device, &motion) == QMA6100P_OK && motion);
    }
}

static void test_odr_data_settling(void)
{
    qma6100p_t device;
    reset();
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    /* Reproduce the reported +/-full-scale, NEWDATA=0 interval around an ODR switch. */
    data_stall_duration_ms = 600U;
    uint32_t before = delay_total;
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 25U) == QMA6100P_OK);
    assert(delay_total - before >= 640U && (regs[0x1A] & 1U) != 0U);
    before = delay_total;
    assert(qma6100p_set_motion_wake(&device, false, 0U, 0U) == QMA6100P_OK);
    assert(delay_total - before >= 610U);
    qma6100p_sample_t sample;
    assert(qma6100p_read(&device, &sample) == QMA6100P_OK);
    assert(sample.x == -64 && sample.y == 128 && sample.z == 1365);
    data_stall_duration_ms = 2000U;
    stale_data_flags = true; /* One old set of NEWDATA flags cannot establish a fresh stream. */
    before = delay_total;
    assert(qma6100p_set_motion_wake(&device, true, 2000U, 25U) == QMA6100P_CONFIG_ERROR);
    assert(delay_total - before <= 1000U && (regs[0x1A] & 1U) == 0U);
}

int main(void)
{
    test_motion_wake();
    test_odr_data_settling();
    test_motion_detector_startup();
    qma6100p_t device;
    qma6100p_sample_t sample;
    reset();
    assert(qma6100p_init(&device, bus) == QMA6100P_OK);
    assert(device.ready && !reset_asserted && delay_total >= 12U);
    assert(startup_writes == sizeof(startup) / sizeof(startup[0]));
    assert(regs[0x0F] == 0x54U && regs[0x10] == 0xE0U); /* +/-8g, preserving high bits */
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
