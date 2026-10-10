#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "commands.h"
#include "common_commands.h"
#include "led_pwm.h"

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

static fake_transport_t transport;
static uint16_t applied_duty;
static unsigned int set_count;

bool serial_uart_read_byte(void *context, uint8_t *byte)
{
    (void)context;
    return fake_read(&transport, byte);
}

void serial_uart_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    fake_write(&transport, data, length);
}

void led_pwm_set_duty(uint16_t duty)
{
    applied_duty = duty;
    ++set_count;
}

static void check_pwm(command_server_t *server, const uint8_t *payload, uint16_t length,
                      command_status_t expected_status)
{
    uint8_t response[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    reset_transport(&transport);
    transport.input_length =
        make_request(transport.input, COMMAND_PROTOCOL_VERSION, 7U, 0x30U, payload, length, false);
    assert(command_server_poll(server));
    size_t response_length = decode_response(&transport, response);
    assert(response[1] == COMMAND_FRAME_FLAG_RESPONSE);
    assert(response[2] == 7U && response[3] == 0x30U);
    assert(response[4] == expected_status);
    if (expected_status == COMMAND_STATUS_OK)
    {
        assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + 2U + 2U);
        assert(response[5] == 2U && response[6] == 0U);
        assert(memcmp(&response[COMMAND_PROTOCOL_HEADER_SIZE], payload, 2U) == 0);
    }
    else
    {
        assert(response_length == COMMAND_PROTOCOL_HEADER_SIZE + 2U);
        assert(response[5] == 0U && response[6] == 0U);
    }
}

int main(void)
{
    command_server_t server;
    const uint16_t duties[] = {0U, 1U, 255U, 256U, 500U, 999U, 1000U, 0U};
    uint8_t payload[3] = {0U};
    uint8_t response[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    application_commands_init(&server);
    for (size_t index = 0U; index < sizeof(duties) / sizeof(duties[0]); ++index)
    {
        payload[0] = (uint8_t)duties[index];
        payload[1] = (uint8_t)(duties[index] >> 8U);
        unsigned int before = set_count;
        check_pwm(&server, payload, 2U, COMMAND_STATUS_OK);
        assert(applied_duty == duties[index] && set_count == before + 1U);
        /* A replay after a lost ACK must leave the same duty. */
        check_pwm(&server, payload, 2U, COMMAND_STATUS_OK);
        assert(applied_duty == duties[index]);
    }
    unsigned int before = set_count;
    payload[0] = 0xE9U;
    payload[1] = 0x03U;
    check_pwm(&server, payload, 2U, COMMAND_STATUS_BAD_ARGUMENT);
    payload[0] = 0xFFU;
    payload[1] = 0xFFU;
    check_pwm(&server, payload, 2U, COMMAND_STATUS_BAD_ARGUMENT);
    for (uint16_t length = 0U; length <= 3U; ++length)
    {
        if (length != 2U)
        {
            check_pwm(&server, payload, length, COMMAND_STATUS_BAD_LENGTH);
        }
    }
    assert(set_count == before && applied_duty == 0U);

    reset_transport(&transport);
    transport.input_length = make_request(transport.input, COMMAND_PROTOCOL_VERSION, 8U,
                                          COMMAND_ID_GET_INFO, NULL, 0U, false);
    assert(command_server_poll(&server));
    assert(decode_response(&transport, response) == COMMAND_PROTOCOL_HEADER_SIZE + 12U + 2U);
    assert(response[4] == COMMAND_STATUS_OK);
    assert(response[COMMAND_PROTOCOL_HEADER_SIZE + 1U] == COMMAND_IMAGE_APPLICATION);
    assert((response[COMMAND_PROTOCOL_HEADER_SIZE + 8U] & (1U << 3U)) != 0U);

    reset_transport(&transport);
    transport.input_length = make_request(transport.input, COMMAND_PROTOCOL_VERSION, 9U,
                                          COMMAND_ID_ENTER_BOOTLOADER, NULL, 0U, false);
    assert(command_server_poll(&server));
    decode_response(&transport, response);
    assert(response[4] == COMMAND_STATUS_OK);
    assert(application_commands_take_bootloader_request());
    assert(!application_commands_take_bootloader_request());
    return 0;
}
