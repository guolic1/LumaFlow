#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "motion_rtc.h"

int main(void)
{
    assert(motion_rtc_ms(999U, 0U, 0x00C101U) == 0U); /* 2000-01-01, weekday ignored. */
    assert(motion_rtc_ms(499U, 0x123456U, 0x000101U) == 45296500U);
    assert(motion_rtc_ms(999U, 0U, 0x000201U) - motion_rtc_ms(0U, 0x235959U, 0x000131U) == 1U);
    assert(motion_rtc_ms(999U, 0U, 0x000229U) - motion_rtc_ms(0U, 0x235959U, 0x000228U) ==
           1U); /* Leap day in 2000. */
    assert(motion_rtc_ms(999U, 0U, 0x240301U) - motion_rtc_ms(0U, 0x235959U, 0x240229U) == 1U);
    assert(motion_rtc_ms(999U, 0U, 0x250301U) - motion_rtc_ms(0U, 0x235959U, 0x250228U) == 1U);
    assert(motion_rtc_ms(999U, 0U, 0x240101U) - motion_rtc_ms(0U, 0x235959U, 0x231231U) == 1U);
    assert(motion_rtc_ms(704U, 0x170247U, 0x000219U) == UINT32_MAX);
    assert(motion_rtc_ms(703U, 0x170247U, 0x000219U) == 0U);
    puts("RTC calendar, leap-year and timestamp wrap tests passed");
    return 0;
}
