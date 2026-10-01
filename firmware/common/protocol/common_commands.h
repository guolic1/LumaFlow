#ifndef COMMON_COMMANDS_H
#define COMMON_COMMANDS_H

#include "command_protocol.h"

#define COMMAND_ID_PING 0x01U
#define COMMAND_ID_GET_INFO 0x02U
#define COMMAND_ID_ENTER_BOOTLOADER 0x10U
#define COMMAND_ID_BOOT_APPLICATION 0x11U

#define COMMAND_IMAGE_BOOTLOADER 1U
#define COMMAND_IMAGE_APPLICATION 2U

#define COMMAND_CAPABILITY_ENTER_BOOTLOADER (1UL << 0U)
#define COMMAND_CAPABILITY_BOOT_APPLICATION (1UL << 1U)

typedef struct
{
    uint8_t image_type;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint32_t capabilities;
} command_device_info_t;

command_status_t command_handle_ping(const void *context, const uint8_t *request,
                                     uint16_t request_length, uint8_t *response,
                                     uint16_t response_capacity, uint16_t *response_length);

command_status_t command_handle_get_info(const void *context, const uint8_t *request,
                                         uint16_t request_length, uint8_t *response,
                                         uint16_t response_capacity, uint16_t *response_length);

#endif
