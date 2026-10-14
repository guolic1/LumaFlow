#ifndef MOTION_RTC_H
#define MOTION_RTC_H

#include <stdint.h>

static inline uint32_t motion_rtc_bcd(uint32_t value)
{
    return (value >> 4U) * 10U + (value & 15U);
}

/* STM32 RTC SSR/TR/DR captured in that order with shadow registers enabled.
 * Calendar is 2000..2099, 24-hour BCD, PREDIV_S=999. Deliberately wrap at 32-bit
 * milliseconds, matching the firmware's timestamps and elapsed-time subtraction. */
static inline uint32_t motion_rtc_ms(uint32_t subseconds, uint32_t time, uint32_t date)
{
    static const uint16_t month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    uint32_t year = motion_rtc_bcd((date >> 16U) & 0xFFU);
    uint32_t month = motion_rtc_bcd((date >> 8U) & 0x1FU);
    uint32_t day = motion_rtc_bcd(date & 0x3FU);
    uint32_t days = year * 365U + (year + 3U) / 4U + month_days[month - 1U] + day - 1U;
    if (month > 2U && year % 4U == 0U)
    {
        ++days;
    }
    uint32_t seconds = days * 86400U + motion_rtc_bcd((time >> 16U) & 0x3FU) * 3600U +
                       motion_rtc_bcd((time >> 8U) & 0x7FU) * 60U + motion_rtc_bcd(time & 0x7FU);
    return seconds * 1000U + 999U - subseconds;
}

#endif
