#include "common_commands.h"

#define DEVICE_INFO_RESPONSE_SIZE 12U

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

command_status_t command_handle_ping(const void *context, const uint8_t *request,
                                     uint16_t request_length, uint8_t *response,
                                     uint16_t response_capacity, uint16_t *response_length)
{
    (void)context;

    if (request_length > response_capacity)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    for (uint16_t index = 0U; index < request_length; ++index)
    {
        response[index] = request[index];
    }
    *response_length = request_length;
    return COMMAND_STATUS_OK;
}

command_status_t command_handle_get_info(const void *context, const uint8_t *request,
                                         uint16_t request_length, uint8_t *response,
                                         uint16_t response_capacity, uint16_t *response_length)
{
    const command_device_info_t *info = context;

    (void)request;
    (void)request_length;

    if ((info == NULL) || (response_capacity < DEVICE_INFO_RESPONSE_SIZE))
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    response[0] = COMMAND_PROTOCOL_VERSION;
    response[1] = info->image_type;
    response[2] = info->version_major;
    response[3] = info->version_minor;
    response[4] = info->version_patch;
    response[5] = 0U;
    write_u16_le(&response[6], COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE);
    write_u32_le(&response[8], info->capabilities);
    *response_length = DEVICE_INFO_RESPONSE_SIZE;
    return COMMAND_STATUS_OK;
}
