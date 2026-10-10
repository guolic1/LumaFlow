#include "commands.h"

#include "common_commands.h"
#include "serial_uart.h"

#define APPLICATION_VERSION_MAJOR 0U
#define APPLICATION_VERSION_MINOR 1U
#define APPLICATION_VERSION_PATCH 0U

static bool enter_bootloader_requested;

static const command_device_info_t application_info = {
    .image_type = COMMAND_IMAGE_APPLICATION,
    .version_major = APPLICATION_VERSION_MAJOR,
    .version_minor = APPLICATION_VERSION_MINOR,
    .version_patch = APPLICATION_VERSION_PATCH,
    .capabilities = COMMAND_CAPABILITY_ENTER_BOOTLOADER,
};

static command_status_t handle_enter_bootloader(const void *context, const uint8_t *request,
                                                uint16_t request_length, uint8_t *response,
                                                uint16_t response_capacity,
                                                uint16_t *response_length)
{
    (void)context;
    (void)request;
    (void)request_length;
    (void)response;
    (void)response_capacity;

    enter_bootloader_requested = true;
    *response_length = 0U;
    return COMMAND_STATUS_OK;
}

static const command_entry_t application_command_table[] = {
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
        .context = &application_info,
    },
    {
        .command = COMMAND_ID_ENTER_BOOTLOADER,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = handle_enter_bootloader,
        .context = NULL,
    },
};

void application_commands_init(command_server_t *server)
{
    const command_transport_t transport = {
        .read_byte = serial_uart_read_byte,
        .write = serial_uart_write,
        .context = NULL,
    };

    enter_bootloader_requested = false;
    command_server_init(server, application_command_table,
                        sizeof(application_command_table) / sizeof(application_command_table[0]),
                        transport);
}

bool application_commands_take_bootloader_request(void)
{
    bool requested = enter_bootloader_requested;

    enter_bootloader_requested = false;
    return requested;
}
