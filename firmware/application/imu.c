#include "imu.h"

#include <string.h>

#include "imu_platform.h"

static qma6100p_t device;
static imu_snapshot_t latest;
static uint32_t handled_ticks;
static bool timer_started;

void imu_init(void)
{
    const qma6100p_bus_t bus = {
        .read = imu_platform_read,
        .write = imu_platform_write,
        .delay_ms = imu_platform_delay_ms,
    };
    if (timer_started)
    {
        imu_platform_stop();
    }
    memset(&latest, 0, sizeof(latest));
    latest.status = IMU_NOT_READY;
    timer_started = imu_platform_start();
    if (!timer_started)
    {
        latest.status = IMU_CONFIG_ERROR;
        return;
    }
    qma6100p_status_t status = qma6100p_init(&device, bus);
    if (status != QMA6100P_OK)
    {
        latest.status = (imu_status_t)status;
        return;
    }
    handled_ticks = imu_platform_ticks();
    latest.ready = true;
}

void imu_poll(void)
{
    qma6100p_sample_t sample;
    uint32_t ticks = imu_platform_ticks();
    if (!latest.ready || (ticks == handled_ticks))
    {
        return;
    }
    latest.missed_ticks += (uint32_t)(ticks - handled_ticks) - 1U;
    handled_ticks = ticks;
    uint32_t timestamp_ms = imu_platform_time_ms();
    qma6100p_status_t status = qma6100p_read(&device, &sample);
    latest.status = (imu_status_t)status;
    if (status != QMA6100P_OK)
    {
        ++latest.read_errors;
        return;
    }
    latest.sample = sample;
    latest.timestamp_ms = timestamp_ms;
    ++latest.sequence;
    latest.valid = true;
}

void imu_stop(void)
{
    if (timer_started)
    {
        imu_platform_stop();
        timer_started = false;
    }
    latest.ready = false;
    latest.status = IMU_STOPPED;
}

void imu_get_snapshot(imu_snapshot_t *snapshot)
{
    *snapshot = latest;
}
