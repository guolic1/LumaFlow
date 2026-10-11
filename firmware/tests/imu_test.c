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

bool imu_platform_start(void)
{
    timer_running = timer_ok;
    return timer_ok;
}
void imu_platform_stop(void)
{
    timer_running = false;
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
    if (reg == 1U)
    {
        ++reads;
    }
    if (!bus_ok)
    {
        return false;
    }
    memcpy(data, &registers[reg], length);
    return true;
}
bool imu_platform_write(uint8_t reg, uint8_t value)
{
    if (!bus_ok)
    {
        return false;
    }
    registers[reg] = value;
    return true;
}

int main(void)
{
    imu_snapshot_t snapshot;
    registers[0] = 0x90U;
    registers[0x33] = 0x05U;
    registers[0x45] = 0xC0U;
    registers[1] = 1U;
    registers[2] = 0x40U; /* +1g */
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
    assert(snapshot.sequence == 1U && snapshot.sample.x == 4096);
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
    assert(snapshot.sequence == 2U && snapshot.sample.x == 4096 && snapshot.valid);
    bus_ok = true;
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(snapshot.status == IMU_OK && snapshot.sequence == 3U && snapshot.read_errors == 1U);
    imu_stop();
    ++ticks;
    imu_poll();
    imu_get_snapshot(&snapshot);
    assert(!timer_running && !snapshot.ready && snapshot.status == IMU_STOPPED && reads == 4U);
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
