#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "imu.h"
#include "imu_platform.h"

static uint8_t registers[256];
static uint32_t ticks;
static unsigned reads;
static bool bus_ok;
static bool timer_ok;
static bool timer_running;
static bool sampling_enabled, motion_armed, motion_pending, interrupt_on_arm;

void imu_platform_set_sampling(bool enabled)
{
    sampling_enabled = enabled;
}
void imu_platform_enable_motion_wake(bool enabled)
{
    motion_armed = enabled;
    motion_pending = false;
}
bool imu_platform_motion_pending(void)
{
    return motion_pending;
}
void imu_platform_clear_motion(void)
{
    motion_pending = false;
}

bool imu_platform_start(void)
{
    timer_running = timer_ok;
    sampling_enabled = timer_ok;
    return timer_ok;
}
void imu_platform_stop(void)
{
    timer_running = false;
    sampling_enabled = false;
}
uint32_t imu_platform_ticks(void)
{
    return ticks;
}
uint32_t imu_platform_time_ms(void)
{
    return ticks * 10U + 1U;
}
void imu_platform_delay_ms(uint32_t duration)
{
    (void)duration;
}
bool imu_platform_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    assert(sampling_enabled); /* Every I2C operation must retain its timeout clock. */
    if (reg == 1U)
    {
        ++reads;
    }
    if (!bus_ok)
    {
        return false;
    }
    memcpy(data, &registers[reg], length);
    if (reg == 0x09U)
    {
        registers[reg] = 0U;
    }
    return true;
}
bool imu_platform_write(uint8_t reg, uint8_t value)
{
    assert(sampling_enabled);
    if (!bus_ok)
    {
        return false;
    }
    registers[reg] = value;
    if (reg == 0x18U && (value & 7U) == 7U && interrupt_on_arm)
    {
        assert(motion_armed);
        motion_pending = true;
        registers[0x09] = 1U;
    }
    return true;
}

int main(void)
{
    imu_snapshot_t snapshot;
    registers[0] = 0x90U;
    registers[0x33] = 0x05U;
    registers[0x45] = 0xC0U;
    registers[1] = 1U;
    registers[2] = 0x10U; /* +1g at +/-8g */
    registers[3] = 1U;
    registers[5] = 1U;
    bus_ok = true;
    timer_ok = true;
    ticks = 0xFFFFFFFEU;
    imu_init();
    imu_get_snapshot(&snapshot);
    assert(snapshot.ready && !snapshot.valid && snapshot.status == IMU_NOT_READY);
    imu_poll();
    assert(reads == 0U);
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(reads == 1U && snapshot.valid && snapshot.status == IMU_OK);
    assert(snapshot.sequence == 1U && snapshot.sample.x == 1024);
    assert(snapshot.timestamp_ms == ticks * 10U + 1U);
    imu_poll();
    assert(reads == 1U);
    ticks += 3U; /* includes wrap; no historical samples can be recovered by rereading */
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(reads == 2U && snapshot.sequence == 2U && snapshot.missed_ticks == 2U);
    bus_ok = false;
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(snapshot.status == IMU_BUS_ERROR && snapshot.read_errors == 1U);
    assert(snapshot.sequence == 2U && snapshot.sample.x == 1024 && snapshot.valid);
    bus_ok = true;
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(snapshot.status == IMU_OK && snapshot.sequence == 3U && snapshot.read_errors == 1U);
    uint32_t before_sequence = snapshot.sequence;
    uint32_t before_missed = snapshot.missed_ticks;
    unsigned before_reads = reads;
    interrupt_on_arm = true;
    assert(imu_set_motion_sleep(true, 2000U));
    assert(motion_armed && !sampling_enabled && registers[0x10] == 0x06U);
    imu_get_snapshot(&snapshot);
    assert(snapshot.ready && !snapshot.valid && snapshot.status == IMU_STOPPED);
    ticks += 30000U;
    imu_poll();
    assert(reads == before_reads); /* No data polling during the suspended interval. */
    bool motion = false;
    assert(imu_check_motion_wake(&motion) && motion && !motion_pending);
    assert(imu_set_motion_sleep(false, 0U));
    assert(!motion_armed && sampling_enabled && registers[0x10] == 0U);
    imu_poll();
    assert(reads == before_reads); /* Rebase scheduling; sleep is not a missed sample. */
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(snapshot.valid && snapshot.sequence == before_sequence + 1U);
    assert(snapshot.missed_ticks == before_missed && snapshot.timestamp_ms == ticks * 10U + 1U);
    interrupt_on_arm = false;
    assert(imu_set_motion_sleep(true, 2000U));
    motion_pending = true; /* EXTI noise with no latched sensor source is ignored. */
    assert(imu_check_motion_wake(&motion) && !motion && !motion_pending);
    bus_ok = false;
    assert(imu_check_motion_wake(&motion) && !motion); /* No IRQ means no I2C access. */
    motion_pending = true;
    assert(!imu_check_motion_wake(&motion));
    imu_get_snapshot(&snapshot);
    assert(snapshot.status == IMU_BUS_ERROR && snapshot.read_errors == 2U);
    assert(!imu_set_motion_sleep(false, 0U)); /* Restoration errors remain visible to the caller. */
    bus_ok = true;
    assert(imu_set_motion_sleep(false, 0U));
    assert(!imu_set_motion_sleep(true, 4000U) && sampling_enabled && !motion_armed);
    imu_get_snapshot(&snapshot);
    assert(snapshot.status == IMU_CONFIG_ERROR);
    imu_stop();
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(!timer_running && !motion_armed && !snapshot.ready && snapshot.status == IMU_STOPPED);
    assert(reads == before_reads + 1U);
    bus_ok = false;
    imu_init();
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(!snapshot.valid && !snapshot.ready && snapshot.status == IMU_BUS_ERROR);
    bus_ok = true;
    timer_ok = false;
    imu_init();
    imu_get_snapshot(&snapshot);
    assert(!snapshot.valid && snapshot.status == IMU_CONFIG_ERROR);
    puts("IMU sampling tests passed");
    return 0;
}
