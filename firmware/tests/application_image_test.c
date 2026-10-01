#include <assert.h>
#include <stdint.h>

#include "application_image.h"

static void test_crc32(void)
{
    static const uint8_t check_value[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc;

    assert(application_crc32(check_value, sizeof(check_value)) == 0xCBF43926UL);

    crc = application_crc32_update(APPLICATION_CRC32_INITIAL, check_value, 4U);
    crc = application_crc32_update(crc, &check_value[4], sizeof(check_value) - 4U);
    assert(application_crc32_finish(crc) == 0xCBF43926UL);
}

static void test_vector_validation(void)
{
    const uint32_t valid_stack_pointer = 0x20002000UL;
    const uint32_t valid_reset_handler = APPLICATION_FLASH_START + 9U;

    assert(application_vector_is_valid(valid_stack_pointer, valid_reset_handler, 16U));
    assert(!application_vector_is_valid(valid_stack_pointer, valid_reset_handler, 7U));
    assert(!application_vector_is_valid(valid_stack_pointer, valid_reset_handler,
                                        APPLICATION_MAX_IMAGE_SIZE + 1U));
    assert(!application_vector_is_valid(0x1FFFFFF8UL, valid_reset_handler, 16U));
    assert(!application_vector_is_valid(0x20002008UL, valid_reset_handler, 16U));
    assert(!application_vector_is_valid(0x20000004UL, valid_reset_handler, 16U));
    assert(!application_vector_is_valid(valid_stack_pointer, valid_reset_handler & ~1UL, 16U));
    assert(!application_vector_is_valid(valid_stack_pointer, APPLICATION_FLASH_START + 1U, 16U));
    assert(!application_vector_is_valid(valid_stack_pointer, APPLICATION_FLASH_START + 17U, 16U));
}

static void test_metadata_validation(void)
{
    application_metadata_t metadata = {
        .magic = APPLICATION_METADATA_MAGIC,
        .format_version = APPLICATION_METADATA_FORMAT_VERSION,
        .header_size = sizeof(application_metadata_t),
        .image_size = 16U,
        .image_crc32 = 0x12345678UL,
        .firmware_version = 0x00010203UL,
        .reserved = 0U,
        .commit_marker = APPLICATION_METADATA_COMMIT_MARKER,
    };

    assert(application_metadata_is_valid(&metadata));

    metadata.commit_marker = 0xFFFFFFFFFFFFFFFFULL;
    assert(!application_metadata_is_valid(&metadata));
    metadata.commit_marker = APPLICATION_METADATA_COMMIT_MARKER;

    metadata.image_size = APPLICATION_MAX_IMAGE_SIZE + 1U;
    assert(!application_metadata_is_valid(&metadata));
    metadata.image_size = 16U;

    metadata.reserved = 1U;
    assert(!application_metadata_is_valid(&metadata));
}

int main(void)
{
    test_crc32();
    test_vector_validation();
    test_metadata_validation();
    return 0;
}
