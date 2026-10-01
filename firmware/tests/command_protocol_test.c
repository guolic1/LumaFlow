#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "command_protocol.h"
#include "common_commands.h"

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

    return 0;
}
