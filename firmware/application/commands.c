#include "commands.h"

#include "common_commands.h"
#include "imu.h"
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
    .capabilities = COMMAND_CAPABILITY_ENTER_BOOTLOADER | COMMAND_CAPABILITY_READ_IMU,
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

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    write_u16_le(data, (uint16_t)value);
    write_u16_le(&data[2], (uint16_t)(value >> 16U));
}

static command_status_t handle_get_imu(const void *context, const uint8_t *request,
                                       uint16_t request_length, uint8_t *response,
                                       uint16_t response_capacity, uint16_t *response_length)
{
    imu_snapshot_t snapshot;
    (void)context;
    (void)request;
    (void)request_length;
    if (response_capacity < 28U)
    {
        return COMMAND_STATUS_INTERNAL_ERROR;
    }
    imu_get_snapshot(&snapshot);
    write_u32_le(&response[0], snapshot.sequence);
    write_u32_le(&response[4], snapshot.timestamp_ms);
    write_u16_le(&response[8], (uint16_t)snapshot.sample.x);
    write_u16_le(&response[10], (uint16_t)snapshot.sample.y);
    write_u16_le(&response[12], (uint16_t)snapshot.sample.z);
    response[14] = snapshot.sample.new_data;
    response[15] = (uint8_t)((snapshot.valid ? 1U : 0U) | (snapshot.ready ? 2U : 0U));
    write_u32_le(&response[16], snapshot.read_errors);
    write_u32_le(&response[20], snapshot.missed_ticks);
    response[24] = (uint8_t)snapshot.status;
    response[25] = IMU_RANGE_G;
    write_u16_le(&response[26], IMU_SAMPLE_RATE_HZ);
    *response_length = 28U;
    return COMMAND_STATUS_OK;
}

static const command_entry_t application_command_table[] = {
    {
        .command = COMMAND_ID_GET_IMU,
        .minimum_length = 0U,
        .maximum_length = 0U,
        .handler = handle_get_imu,
        .context = NULL,
    },
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
