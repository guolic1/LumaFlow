#include "application_image.h"

#include <stddef.h>

#define SRAM_START 0x20000000UL
#define SRAM_END 0x20002000UL

_Static_assert(sizeof(application_metadata_t) == APPLICATION_METADATA_SIZE,
               "Application metadata must occupy 32 bytes");
_Static_assert(offsetof(application_metadata_t, commit_marker) == 24U,
               "Commit marker must be the final flash double word");
_Static_assert((APPLICATION_FLASH_START & 0x7U) == 0U,
               "Application start must be double-word aligned");
_Static_assert((APPLICATION_METADATA_ADDRESS & 0x7U) == 0U,
               "Application metadata must be double-word aligned");
_Static_assert(APPLICATION_METADATA_ADDRESS + APPLICATION_METADATA_SIZE == APPLICATION_FLASH_END,
               "Application metadata must occupy the end of flash");

uint32_t application_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }

    return crc;
}

uint32_t application_crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t application_crc32(const uint8_t *data, size_t length)
{
    return application_crc32_finish(
        application_crc32_update(APPLICATION_CRC32_INITIAL, data, length));
}

bool application_vector_is_valid(uint32_t stack_pointer, uint32_t reset_handler,
                                 uint32_t image_size)
{
    uint32_t reset_address = reset_handler & ~1UL;
    uint32_t image_end;

    if ((image_size < 8U) || (image_size > APPLICATION_MAX_IMAGE_SIZE))
    {
        return false;
    }

    image_end = APPLICATION_FLASH_START + image_size;
    return (stack_pointer >= SRAM_START) && (stack_pointer <= SRAM_END) &&
           ((stack_pointer & 0x7U) == 0U) && ((reset_handler & 1U) != 0U) &&
           (reset_address >= (APPLICATION_FLASH_START + 8U)) && (reset_address < image_end);
}

bool application_metadata_is_valid(const application_metadata_t *metadata)
{
    return (metadata->commit_marker == APPLICATION_METADATA_COMMIT_MARKER) &&
           (metadata->magic == APPLICATION_METADATA_MAGIC) &&
           (metadata->format_version == APPLICATION_METADATA_FORMAT_VERSION) &&
           (metadata->header_size == sizeof(application_metadata_t)) &&
           (metadata->image_size >= 8U) && (metadata->image_size <= APPLICATION_MAX_IMAGE_SIZE) &&
           (metadata->reserved == 0U);
}

bool application_image_is_valid(void)
{
    const volatile application_metadata_t *stored_metadata =
        (const volatile application_metadata_t *)(uintptr_t)APPLICATION_METADATA_ADDRESS;
    const volatile uint32_t *vector = (const volatile uint32_t *)(uintptr_t)APPLICATION_FLASH_START;
    application_metadata_t metadata = *stored_metadata;

    if (!application_metadata_is_valid(&metadata))
    {
        return false;
    }
    if (!application_vector_is_valid(vector[0], vector[1], metadata.image_size))
    {
        return false;
    }

    return application_crc32((const uint8_t *)(uintptr_t)APPLICATION_FLASH_START,
                             metadata.image_size) == metadata.image_crc32;
}
