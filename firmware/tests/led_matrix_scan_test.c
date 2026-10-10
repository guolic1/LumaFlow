#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "led_matrix_scan.h"

/* Independent topology rule checked against the KiCad netlist: cathodes IO0..16,
 * anodes IO0..17, with this diagonal band. D numbers increase by cathode/anode. */
static bool connected(unsigned int anode, unsigned int cathode)
{
    return anode != cathode && cathode < 17U && anode <= cathode + 9U && cathode <= anode + 8U;
}

static unsigned int check_frame(led_matrix_scan_t *scan, const bool expected[LED_MATRIX_LED_COUNT])
{
    bool seen[LED_MATRIX_LED_COUNT] = {false};
    unsigned int pairs = 0U;

    for (unsigned int slot = 0U; slot < LED_MATRIX_SLOT_COUNT; ++slot)
    {
        led_matrix_drive_t drive = led_matrix_scan_next(scan);
        unsigned int lit = 0U;
        unsigned int index = 0U;
        unsigned int high_count = 0U;
        unsigned int low_count = 0U;

        assert((drive.high & drive.low) == 0U);
        assert(((drive.high | drive.low) >> 18U) == 0U);
        for (unsigned int pin = 0U; pin < 18U; ++pin)
        {
            high_count += (drive.high >> pin) & 1U;
            low_count += (drive.low >> pin) & 1U;
            unsigned int loads = 0U;
            unsigned int sources = 0U;
            for (unsigned int other = 0U; other < 18U; ++other)
            {
                loads += connected(pin, other) && ((drive.low & (1UL << other)) != 0U);
                sources += connected(other, pin) && ((drive.high & (1UL << other)) != 0U);
            }
            if ((drive.high & (1UL << pin)) != 0U)
            {
                assert(loads == 1U);
            }
            if ((drive.low & (1UL << pin)) != 0U)
            {
                assert(sources == 1U);
            }
        }
        assert(high_count == low_count && high_count <= 2U);
        for (unsigned int cathode = 0U; cathode < 17U; ++cathode)
        {
            for (unsigned int anode = 0U; anode < 18U; ++anode)
            {
                if (!connected(anode, cathode))
                {
                    continue;
                }
                if (((drive.high & (1UL << anode)) != 0U) && ((drive.low & (1UL << cathode)) != 0U))
                {
                    assert(expected[index]);
                    assert(!seen[index]);
                    seen[index] = true;
                    ++lit;
                }
                ++index;
            }
        }
        assert(index == LED_MATRIX_LED_COUNT);
        assert(lit == high_count);
        pairs += lit == 2U;
    }
    assert(memcmp(seen, expected, sizeof(seen)) == 0);
    return pairs;
}

static void test_coverage_and_individual_pixels(void)
{
    led_matrix_scan_t scan;
    bool pixels[LED_MATRIX_LED_COUNT] = {false};

    led_matrix_scan_init(&scan);
    assert(check_frame(&scan, pixels) == 0U);
    assert(!led_matrix_scan_submit(&scan, NULL));
    for (unsigned int index = 0U; index < LED_MATRIX_LED_COUNT; ++index)
    {
        pixels[index] = true;
    }
    assert(led_matrix_scan_submit(&scan, pixels));
    assert(check_frame(&scan, pixels) == 56U);

    memset(pixels, 0, sizeof(pixels));
    for (unsigned int index = 0U; index < LED_MATRIX_LED_COUNT; ++index)
    {
        pixels[index] = true;
        assert(led_matrix_scan_submit(&scan, pixels));
        assert(check_frame(&scan, pixels) == 0U);
        pixels[index] = false;
    }
}

static void test_frame_boundary_and_copy(void)
{
    led_matrix_scan_t scan;
    bool pixels[LED_MATRIX_LED_COUNT] = {false};
    bool expected[LED_MATRIX_LED_COUNT] = {false};

    led_matrix_scan_init(&scan);
    led_matrix_drive_t first = led_matrix_scan_next(&scan);
    assert(first.high == 0U && first.low == 0U);
    pixels[0] = true;
    expected[0] = true;
    assert(led_matrix_scan_submit(&scan, pixels));
    pixels[0] = false;
    assert(!led_matrix_scan_submit(&scan, pixels));
    for (unsigned int slot = 1U; slot < LED_MATRIX_SLOT_COUNT; ++slot)
    {
        led_matrix_drive_t drive = led_matrix_scan_next(&scan);
        assert(drive.high == 0U && drive.low == 0U);
    }
    assert(check_frame(&scan, expected) == 0U);
    assert(led_matrix_scan_submit(&scan, pixels));
    assert(check_frame(&scan, pixels) == 0U);

    /* Reinitialization discards both the active image and any pending image. */
    assert(led_matrix_scan_submit(&scan, expected));
    led_matrix_scan_init(&scan);
    assert(check_frame(&scan, pixels) == 0U);
}

int main(void)
{
    test_coverage_and_individual_pixels();
    test_frame_boundary_and_copy();
    return 0;
}
