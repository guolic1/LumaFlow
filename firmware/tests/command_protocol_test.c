#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "command_protocol.h"
#include "common_commands.h"
#include "commands.h"
#include "fluid_effect.h"
#include "imu.h"
#include "serial_uart.h"

#define TEST_BUFFER_SIZE 512U

typedef struct
{
    uint8_t input[TEST_BUFFER_SIZE];
    size_t input_length;
    size_t input_position;
    uint8_t output[TEST_BUFFER_SIZE];
    size_t output_length;
} fake_transport_t;

static uint16_t test_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t index = 0U; index < length; ++index)
    {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc =
                ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
        }
    }

    return crc;
}

static size_t test_cobs_encode(const uint8_t *input, size_t length, uint8_t *output)
{
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    while (read_index < length)
    {
        if (input[read_index] == 0U)
        {
            output[code_index] = code;
            code = 1U;
            code_index = write_index++;
        }
        else
        {
            output[write_index++] = input[read_index];
            ++code;
            if (code == 0xFFU)
            {
                output[code_index] = code;
                code = 1U;
                code_index = write_index++;
            }
        }
        ++read_index;
    }

    output[code_index] = code;
    return write_index;
}

static size_t test_cobs_decode(const uint8_t *input, size_t length, uint8_t *output,
                               size_t capacity)
{
    size_t read_index = 0U;
    size_t write_index = 0U;

    while (read_index < length)
    {
        uint8_t code = input[read_index++];
        assert(code != 0U);

        for (uint8_t offset = 1U; offset < code; ++offset)
        {
            assert(read_index < length);
            assert(write_index < capacity);
            output[write_index++] = input[read_index++];
        }

        if ((code != 0xFFU) && (read_index < length))
        {
            assert(write_index < capacity);
            output[write_index++] = 0U;
        }
    }

    return write_index;
}

static bool fake_read(void *context, uint8_t *byte)
{
    fake_transport_t *transport = context;

    if (transport->input_position >= transport->input_length)
    {
        return false;
    }

    *byte = transport->input[transport->input_position++];
    return true;
}

static void fake_write(void *context, const uint8_t *data, size_t length)
{
    fake_transport_t *transport = context;

    assert((transport->output_length + length) <= sizeof(transport->output));
    memcpy(&transport->output[transport->output_length], data, length);
    transport->output_length += length;
}

static size_t make_request(uint8_t *output, uint8_t version, uint8_t sequence, uint8_t command,
                           const uint8_t *payload, uint16_t payload_length, bool corrupt_crc)
{
    uint8_t raw[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    size_t raw_length = COMMAND_PROTOCOL_HEADER_SIZE + payload_length;
    uint16_t crc;
    size_t encoded_length;

    raw[0] = version;
    raw[1] = 0U;
    raw[2] = sequence;
    raw[3] = command;
    raw[4] = 0U;
    raw[5] = (uint8_t)payload_length;
    raw[6] = (uint8_t)(payload_length >> 8U);
    if (payload_length != 0U)
    {
        memcpy(&raw[COMMAND_PROTOCOL_HEADER_SIZE], payload, payload_length);
    }

    crc = test_crc16(raw, raw_length);
    if (corrupt_crc)
    {
        crc ^= 1U;
    }
    raw[raw_length++] = (uint8_t)crc;
    raw[raw_length++] = (uint8_t)(crc >> 8U);

    encoded_length = test_cobs_encode(raw, raw_length, output);
    output[encoded_length++] = 0U;
    return encoded_length;
}

static size_t decode_response(const fake_transport_t *transport, uint8_t *response)
{
    size_t response_length;
    uint16_t received_crc;

    assert(transport->output_length > 1U);
    assert(transport->output[transport->output_length - 1U] == 0U);
    response_length = test_cobs_decode(transport->output, transport->output_length - 1U, response,
                                       COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE);
    assert(response_length >= 2U);
    received_crc =
        (uint16_t)response[response_length - 2U] | ((uint16_t)response[response_length - 1U] << 8U);
    assert(test_crc16(response, response_length - 2U) == received_crc);
    return response_length;
}

static void reset_transport(fake_transport_t *transport)
{
    memset(transport, 0, sizeof(*transport));
}

static fake_transport_t application_transport;
static imu_snapshot_t imu_snapshot;

bool serial_uart_read_byte(void *context, uint8_t *byte)
{
    (void)context;
    return fake_read(&application_transport, byte);
}

void serial_uart_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    fake_write(&application_transport, data, length);
}

void imu_get_snapshot(imu_snapshot_t *snapshot)
{
    *snapshot = imu_snapshot;
}

void fluid_effect_get_stats(fluid_effect_stats_t *stats)
{
    *stats = (fluid_effect_stats_t){0x01020304U, 1200U, 2400U, 3000U, 7U, 190U, 3650U};
}

static void test_application_fluid(void)
{
    command_server_t server;
    uint8_t response[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    static const uint8_t expected[] = {
        4, 3, 2, 1, 0xB0, 4, 0, 0, 0x60, 9, 0,  0,  0xB8, 0x0B, 0,  0,
        7, 0, 0, 0, 190,  0, 0, 0, 64,   0, 13, 13, 0x42, 0x0E, 30, 0,
    };
    application_commands_init(&server);
    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 46U, 0x31U, NULL, 0U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 41U);
    assert(response[4] == COMMAND_STATUS_OK && response[2] == 46U && response[3] == 0x31U);
    assert(memcmp(&response[7], expected, sizeof(expected)) == 0);
    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 47U, 0x31U, expected, 1U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 9U);
    assert(response[4] == COMMAND_STATUS_BAD_LENGTH);
}

static void test_application_imu(void)
{
    command_server_t server;
    uint8_t response[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    static const uint8_t expected[] = {
        4, 3, 2, 1, 0x40, 0x30, 0x20, 0x10, 0, 0xE0, 0xFF, 0x1F, 0,   0xF0,
        5, 3, 2, 0, 0,    0,    3,    0,    0, 0,    0,    2,    100, 0,
    };
    imu_snapshot = (imu_snapshot_t){
        .sample = {-8192, 8191, -4096, 5U},
        .sequence = 0x01020304U,
        .timestamp_ms = 0x10203040U,
        .read_errors = 2U,
        .missed_ticks = 3U,
        .status = IMU_OK,
        .valid = true,
        .ready = true,
    };
    application_commands_init(&server);
    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 42U, 0x30U, NULL, 0U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 37U);
    assert(response[4] == COMMAND_STATUS_OK && response[2] == 42U && response[3] == 0x30U);
    assert(memcmp(&response[7], expected, sizeof(expected)) == 0);

    imu_snapshot.valid = false;
    imu_snapshot.ready = false;
    imu_snapshot.status = IMU_CHIP_ID_ERROR;
    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 43U, 0x30U, NULL, 0U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 37U);
    assert(response[7U + 15U] == 0U && response[7U + 24U] == IMU_CHIP_ID_ERROR);

    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 44U, 0x30U, expected, 1U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 9U);
    assert(response[4] == COMMAND_STATUS_BAD_LENGTH);

    reset_transport(&application_transport);
    application_transport.input_length =
        make_request(application_transport.input, 1U, 45U, COMMAND_ID_GET_INFO, NULL, 0U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&application_transport, response) == 21U);
    assert(response[7U + 8U] == 25U); /* bootloader entry + IMU + fluid stats */
    assert(!application_commands_take_bootloader_request());
}

int main(void)
{
    static const command_device_info_t device_info = {
        .image_type = COMMAND_IMAGE_APPLICATION,
        .version_major = 1U,
        .version_minor = 2U,
        .version_patch = 3U,
        .capabilities = COMMAND_CAPABILITY_ENTER_BOOTLOADER,
    };
    static const command_entry_t commands[] = {
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
            .context = &device_info,
        },
    };
    fake_transport_t transport;
    command_server_t server;
    uint8_t response[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    const uint8_t ping_payload[] = {0x00U, 0x11U, 0x00U, 0xFFU};
    size_t response_length;

    reset_transport(&transport);
    command_server_init(&server, commands, sizeof(commands) / sizeof(commands[0]),
                        (command_transport_t){fake_read, fake_write, &transport});

    transport.input_length =
        make_request(transport.input, COMMAND_PROTOCOL_VERSION, 7U, COMMAND_ID_PING, ping_payload,
                     sizeof(ping_payload), false);
    assert(command_server_poll(&server));
    response_length = decode_response(&transport, response);
    assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + sizeof(ping_payload) + 2U);
    assert(response[1] == COMMAND_FRAME_FLAG_RESPONSE);
    assert(response[2] == 7U);
    assert(response[3] == COMMAND_ID_PING);
    assert(response[4] == COMMAND_STATUS_OK);
    assert(response[5] == sizeof(ping_payload));
    assert(memcmp(&response[COMMAND_PROTOCOL_HEADER_SIZE], ping_payload, sizeof(ping_payload)) ==
           0);

    reset_transport(&transport);
    transport.input_length = make_request(transport.input, COMMAND_PROTOCOL_VERSION, 8U,
                                          COMMAND_ID_GET_INFO, NULL, 0U, false);
    assert(command_server_poll(&server));
    response_length = decode_response(&transport, response);
    assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + 12U + 2U);
    assert(response[4] == COMMAND_STATUS_OK);
    assert(response[5] == 12U);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE] == COMMAND_PROTOCOL_VERSION);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE + 1U] == COMMAND_IMAGE_APPLICATION);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE + 2U] == 1U);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE + 3U] == 2U);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE + 4U] == 3U);

    reset_transport(&transport);
    transport.input_length =
        make_request(transport.input, COMMAND_PROTOCOL_VERSION, 9U, 0x7FU, NULL, 0U, false);
    assert(command_server_poll(&server));
    response_length = decode_response(&transport, response);
    assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + 2U);
    assert(response[4] == COMMAND_STATUS_BAD_COMMAND);

    reset_transport(&transport);
    transport.input_length = make_request(transport.input, COMMAND_PROTOCOL_VERSION, 10U,
                                          COMMAND_ID_GET_INFO, NULL, 0U, true);
    assert(!command_server_poll(&server));
    assert(transport.output_length == 0U);

    reset_transport(&transport);
    transport.input_length = make_request(transport.input, COMMAND_PROTOCOL_VERSION + 1U, 11U,
                                          COMMAND_ID_GET_INFO, NULL, 0U, false);
    assert(command_server_poll(&server));
    response_length = decode_response(&transport, response);
    assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + 2U);
    assert(response[4] == COMMAND_STATUS_UNSUPPORTED_VERSION);

    test_application_fluid();
    test_application_imu();
    return 0;
}
