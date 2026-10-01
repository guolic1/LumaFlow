#include "commands.h"

#include "application_update.h"
#include "common_commands.h"
#include "jump_to_application.h"
#include "serial_uart.h"

#define BOOTLOADER_VERSION_MAJOR 0U
#define BOOTLOADER_VERSION_MINOR 2U
#define BOOTLOADER_VERSION_PATCH 0U

_Static_assert(4U + APPLICATION_UPDATE_MAX_CHUNK_SIZE <= COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE,
               "Update request must fit in one protocol frame");

static bool jump_requested;

static const command_device_info_t bootloader_info = {
    .image_type = COMMAND_IMAGE_BOOTLOADER,
    .version_major = BOOTLOADER_VERSION_MAJOR,
    .version_minor = BOOTLOADER_VERSION_MINOR,
    .version_patch = BOOTLOADER_VERSION_PATCH,
    .capabilities = COMMAND_CAPABILITY_BOOT_APPLICATION | COMMAND_CAPABILITY_UPDATE_APPLICATION,
};

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

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

static command_status_t command_status_from_update_result(application_update_result_t result)
{
    switch (result)
    {
    case APPLICATION_UPDATE_RESULT_OK:
        return COMMAND_STATUS_OK;
    case APPLICATION_UPDATE_RESULT_BAD_STATE:
        return COMMAND_STATUS_BAD_STATE;
    case APPLICATION_UPDATE_RESULT_BAD_ARGUMENT:
        return COMMAND_STATUS_BAD_ARGUMENT;
    case APPLICATION_UPDATE_RESULT_FLASH_ERROR:
        return COMMAND_STATUS_FLASH_ERROR;
    case APPLICATION_UPDATE_RESULT_VERIFY_ERROR:
        return COMMAND_STATUS_VERIFY_ERROR;
    default:
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
}

static command_status_t handle_boot_application(const void *context, const uint8_t *request,
                                                uint16_t request_length, uint8_t *response,
                                                uint16_t response_capacity,
                                                uint16_t *response_length)
{
    (void)context;
    (void)request;
    (void)request_length;
    (void)response;
    (void)response_capacity;

    if (!bootloader_application_is_valid())
    {
        return COMMAND_STATUS_BAD_STATE;
    }

    jump_requested = true;
    *response_length = 0U;
    return COMMAND_STATUS_OK;
}

static command_status_t handle_begin_update(const void *context, const uint8_t *request,
                                            uint16_t request_length, uint8_t *response,
                                            uint16_t response_capacity, uint16_t *response_length)
{
    application_update_result_t result;

    (void)context;
    (void)request_length;
    if (response_capacity < 8U)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    result = application_update_begin(read_u32_le(request), read_u32_le(&request[4]),
                                      read_u32_le(&request[8]));
    if (result != APPLICATION_UPDATE_RESULT_OK)
    {
        return command_status_from_update_result(result);
    }

    write_u16_le(response, APPLICATION_UPDATE_MAX_CHUNK_SIZE);
    write_u16_le(&response[2], 0U);
    write_u32_le(&response[4], 0U);
    *response_length = 8U;
    return COMMAND_STATUS_OK;
}

static command_status_t handle_write_chunk(const void *context, const uint8_t *request,
                                           uint16_t request_length, uint8_t *response,
                                           uint16_t response_capacity, uint16_t *response_length)
{
    application_update_result_t result;
    application_update_status_t status;

    (void)context;
    if (response_capacity < 4U)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    result = application_update_write(read_u32_le(request), &request[4], request_length - 4U);
    if (result != APPLICATION_UPDATE_RESULT_OK)
    {
        return command_status_from_update_result(result);
    }

    application_update_get_status(&status);
    write_u32_le(response, status.next_offset);
    *response_length = 4U;
    return COMMAND_STATUS_OK;
}

static command_status_t handle_end_update(const void *context, const uint8_t *request,
                                          uint16_t request_length, uint8_t *response,
                                          uint16_t response_capacity, uint16_t *response_length)
{
    application_update_result_t result;

    (void)context;
    (void)request;
    (void)request_length;
    (void)response;
    (void)response_capacity;

    result = application_update_end();
    if (result != APPLICATION_UPDATE_RESULT_OK)
    {
        return command_status_from_update_result(result);
    }

    *response_length = 0U;
    return COMMAND_STATUS_OK;
}

static command_status_t handle_get_update_status(const void *context, const uint8_t *request,
                                                 uint16_t request_length, uint8_t *response,
                                                 uint16_t response_capacity,
                                                 uint16_t *response_length)
{
    application_update_status_t status;

    (void)context;
    (void)request;
    (void)request_length;
    if (response_capacity < 12U)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }

    application_update_get_status(&status);
    response[0] = (uint8_t)status.state;
    response[1] = (uint8_t)status.last_result;
    write_u16_le(&response[2], 0U);
    write_u32_le(&response[4], status.next_offset);
    write_u32_le(&response[8], status.image_size);
    *response_length = 12U;
    return COMMAND_STATUS_OK;
}

static const command_entry_t bootloader_command_table[] = {
    {
        .command = COMMAND_ID_PING,
        .minimum_length = 0U,
        .maximum_length = 32U,
        .handler = command_handle_ping,
        .context = NULL,
    },
    {
        .command = COMMAND_ID_GET_INFO,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = command_handle_get_info,
        .context = &bootloader_info,
    },
    {
        .command = COMMAND_ID_BOOT_APPLICATION,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = handle_boot_application,
        .context = NULL,
    },
    {
        .command = COMMAND_ID_BEGIN_UPDATE,
        .minimum_length = 12U,
        .maximum_length = 12U,
        .handler = handle_begin_update,
        .context = NULL,
    },
    {
        .command = COMMAND_ID_WRITE_CHUNK,
        .minimum_length = 5U,
        .maximum_length = 4U + APPLICATION_UPDATE_MAX_CHUNK_SIZE,
        .handler = handle_write_chunk,
        .context = NULL,
    },
    {
        .command = COMMAND_ID_END_UPDATE,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = handle_end_update,
        .context = NULL,
    },
    {
        .command = COMMAND_ID_GET_UPDATE_STATUS,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = handle_get_update_status,
        .context = NULL,
    },
};

void bootloader_commands_init(command_server_t *server)
{
    const command_transport_t transport = {
        .read_byte = serial_uart_read_byte,
        .write = serial_uart_write,
        .context = NULL,
    };

    jump_requested = false;
    application_update_init();
    command_server_init(server, bootloader_command_table,
                        sizeof(bootloader_command_table) / sizeof(bootloader_command_table[0]),
                        transport);
}

bool bootloader_commands_take_jump_request(void)
{
    bool requested = jump_requested;

    jump_requested = false;
    return requested;
}
