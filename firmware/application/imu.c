#include "imu.h"

#include <string.h>

#include "imu_platform.h"

static qma6100p_t device;
static imu_snapshot_t latest;
static uint32_t handled_ticks;
static bool timer_started;
static bool motion_sleeping;

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
    motion_sleeping = false;
    imu_platform_enable_motion_wake(false);
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
    if (!latest.ready || motion_sleeping || (ticks == handled_ticks))
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
    imu_platform_enable_motion_wake(false);
    motion_sleeping = false;
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

bool imu_set_motion_sleep(bool sleeping, uint16_t threshold_mg)
{
    if (!latest.ready)
    {
        return false;
    }
    if (sleeping == motion_sleeping)
    {
        return true;
    }
    if (sleeping)
    {
        /* Input/polarity/reset changes may generate edges while configuring the IMU. */
        imu_platform_enable_motion_wake(false);
    }
    else
    {
        /* I2C deadlines and ODR settling need the microsecond timer running. */
        imu_platform_set_sampling(true);
    }
    qma6100p_status_t status =
        qma6100p_set_motion_wake(&device, sleeping, threshold_mg, IMU_SLEEP_SAMPLE_RATE_HZ);
    if (status != QMA6100P_OK)
    {
        latest.status = (imu_status_t)status;
        latest.read_errors += status == QMA6100P_BUS_ERROR ? 1U : 0U;
        if (sleeping)
        {
            imu_platform_enable_motion_wake(false);
        }
        return false;
    }
    if (sleeping)
    {
        /* Discard configuration edges only after the detector is ready. INT1 is
         * latched, and the platform also checks its level, so a real event between
         * sensor mapping and EXTI arming is retained. */
        imu_platform_enable_motion_wake(true);
        imu_platform_set_sampling(false);
    }
    else
    {
        imu_platform_enable_motion_wake(false);
    }
    handled_ticks = imu_platform_ticks();
    motion_sleeping = sleeping;
    latest.valid = false;
    latest.status = sleeping ? IMU_STOPPED : IMU_NOT_READY;
    return true;
}

bool imu_check_motion_wake(bool *motion)
{
    *motion = false;
    if (!motion_sleeping || !imu_platform_motion_pending())
    {
        return true;
    }
    /* Clear the software flag before I2C, preserving any new edge during the read.
     * Reading 0x09 also clears the sensor latch. A short impulse stays recorded. */
    imu_platform_clear_motion();
    imu_platform_set_sampling(true); /* Keep bounded I2C timeouts while checking the latch. */
    qma6100p_status_t status = qma6100p_read_motion(&device, motion);
    imu_platform_set_sampling(false);
    if (status != QMA6100P_OK)
    {
        latest.status = (imu_status_t)status;
        latest.read_errors += status == QMA6100P_BUS_ERROR ? 1U : 0U;
    }
    return status == QMA6100P_OK;
}
