#ifndef APPLICATION_IMAGE_H
#define APPLICATION_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APPLICATION_FLASH_START 0x08002000UL
#define APPLICATION_FLASH_END 0x08010000UL
#define APPLICATION_METADATA_ADDRESS 0x0800FFE0UL
#define APPLICATION_METADATA_SIZE 32UL
#define APPLICATION_MAX_IMAGE_SIZE (APPLICATION_METADATA_ADDRESS - APPLICATION_FLASH_START)

#define APPLICATION_METADATA_MAGIC 0x414D554CUL
#define APPLICATION_METADATA_FORMAT_VERSION 1U
#define APPLICATION_METADATA_COMMIT_MARKER 0x4C554D4141505031ULL

#define APPLICATION_CRC32_INITIAL 0xFFFFFFFFUL

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t firmware_version;
    uint32_t reserved;
    uint64_t commit_marker;
} application_metadata_t;

uint32_t application_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t application_crc32_finish(uint32_t crc);
uint32_t application_crc32(const uint8_t *data, size_t length);

bool application_vector_is_valid(uint32_t stack_pointer, uint32_t reset_handler,
                                 uint32_t image_size);
bool application_metadata_is_valid(const application_metadata_t *metadata);
bool application_image_is_valid(void);

#endif
