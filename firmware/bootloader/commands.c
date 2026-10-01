#include "commands.h"

#include "common_commands.h"
#include "jump_to_application.h"
#include "serial_uart.h"

#define BOOTLOADER_VERSION_MAJOR 0U
#define BOOTLOADER_VERSION_MINOR 1U
#define BOOTLOADER_VERSION_PATCH 0U

static bool jump_requested;

static const command_device_info_t bootloader_info = {
    .image_type = COMMAND_IMAGE_BOOTLOADER,
    .version_major = BOOTLOADER_VERSION_MAJOR,
    .version_minor = BOOTLOADER_VERSION_MINOR,
    .version_patch = BOOTLOADER_VERSION_PATCH,
    .capabilities = COMMAND_CAPABILITY_BOOT_APPLICATION,
};

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
};

void bootloader_commands_init(command_server_t *server)
{
    const command_transport_t transport = {
        .read_byte = serial_uart_read_byte,
        .write = serial_uart_write,
        .context = NULL,
    };

    jump_requested = false;
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
