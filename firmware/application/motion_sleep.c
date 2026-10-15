#include "motion_sleep.h"

#include <stdint.h>
#include <string.h>

#include "fluid_effect.h"
#include "imu.h"
#include "imu_platform.h"
#include "led_matrix.h"
#include "motion_sleep_config.h"
#include "serial_uart.h"

_Static_assert(MOTION_SLEEP_IDLE_MS > 0U && MOTION_SLEEP_IDLE_MS <= INT32_MAX &&
                   MOTION_SLEEP_MAX_SAMPLE_GAP_MS > 0U &&
                   MOTION_SLEEP_MAX_SAMPLE_GAP_MS < MOTION_SLEEP_IDLE_MS,
               "Motion sleep timing must fit wrap-safe comparisons");
_Static_assert(MOTION_SLEEP_QUIET_THRESHOLD_RAW > 0 &&
                   MOTION_SLEEP_WAKE_THRESHOLD_RAW >= MOTION_SLEEP_QUIET_THRESHOLD_RAW &&
                   MOTION_SLEEP_WAKE_THRESHOLD_RAW <= 255U * 16U,
               "Wake threshold must fit the sensor's 8-bit motion threshold");

static struct
{
    uint32_t last_motion_ms;
    uint32_t last_sample_ms;
    uint32_t sequence;
    int16_t x, y, z;
    bool have_reference;
    bool sleeping;
} activity;

static void anchor(const imu_snapshot_t *snapshot)
{
    activity.x = snapshot->sample.x;
    activity.y = snapshot->sample.y;
    activity.z = snapshot->sample.z;
    activity.last_motion_ms = snapshot->timestamp_ms;
    activity.last_sample_ms = snapshot->timestamp_ms;
    activity.sequence = snapshot->sequence;
    activity.have_reference = true;
}

static bool wake(void)
{
    if (activity.sleeping)
    {
        if (!imu_set_motion_sleep(false, 0U) || !led_matrix_init())
        {
            return false;
        }
        serial_uart_init();
        fluid_effect_set_paused(false);
        activity.sleeping = false;
        activity.have_reference = false; /* Anchor the first fresh 100Hz sample after wake. */
    }
    return true;
}

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

void motion_sleep_init(void)
{
    memset(&activity, 0, sizeof(activity));
}

bool motion_sleep_poll(void)
{
    if (activity.sleeping)
    {
        bool motion;
        if (!imu_check_motion_wake(&motion) || motion)
        {
            return wake();
        }
        return true; /* Stale/absent CPU samples are expected while the detector runs alone. */
    }
    imu_snapshot_t snapshot;
    imu_get_snapshot(&snapshot);
    bool healthy = snapshot.ready && snapshot.valid && snapshot.status == IMU_OK &&
                   (uint32_t)(imu_platform_time_ms() - snapshot.timestamp_ms) <=
                       MOTION_SLEEP_MAX_SAMPLE_GAP_MS;
    if (!healthy)
    {
        /* Missing/failed measurements cannot prove stillness or guarantee motion wake. */
        activity.have_reference = false;
        return wake();
    }
    if (activity.have_reference && snapshot.sequence == activity.sequence)
    {
        return true;
    }
    if (!activity.have_reference || (uint32_t)(snapshot.timestamp_ms - activity.last_sample_ms) >
                                        MOTION_SLEEP_MAX_SAMPLE_GAP_MS)
    {
        anchor(&snapshot);
        return wake();
    }
    activity.sequence = snapshot.sequence;
    activity.last_sample_ms = snapshot.timestamp_ms;
    int32_t threshold = (int32_t)MOTION_SLEEP_QUIET_THRESHOLD_RAW;
    /* Keep the reference fixed until motion: slow tilts accumulate, instead of
     * disappearing when comparing only adjacent 100Hz measurements. */
    if (absolute((int32_t)snapshot.sample.x - activity.x) >= threshold ||
        absolute((int32_t)snapshot.sample.y - activity.y) >= threshold ||
        absolute((int32_t)snapshot.sample.z - activity.z) >= threshold)
    {
        anchor(&snapshot);
        return wake();
    }
    if (!activity.sleeping &&
        (uint32_t)(snapshot.timestamp_ms - activity.last_motion_ms) >= MOTION_SLEEP_IDLE_MS)
    {
        fluid_effect_set_paused(true);
        led_matrix_stop();
        serial_uart_flush();
        serial_uart_deinit();
        /* Settle the IMU after switching display/UART loads, so their transitions
         * are cleared before motion wake is armed. Failure remains fatal to main. */
        if (!imu_set_motion_sleep(true, MOTION_SLEEP_WAKE_THRESHOLD_MG))
        {
            return false;
        }
        activity.sleeping = true;
    }
    return true;
}

bool motion_sleep_is_sleeping(void)
{
    return activity.sleeping;
}
