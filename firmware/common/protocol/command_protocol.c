#include "command_protocol.h"

#define FRAME_VERSION_INDEX 0U
#define FRAME_FLAGS_INDEX 1U
#define FRAME_SEQUENCE_INDEX 2U
#define FRAME_COMMAND_INDEX 3U
#define FRAME_STATUS_INDEX 4U
#define FRAME_LENGTH_LOW_INDEX 5U
#define FRAME_LENGTH_HIGH_INDEX 6U

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t index = 0; index < length; ++index)
    {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0; bit < 8U; ++bit)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

static size_t cobs_encode(const uint8_t *input, size_t input_length, uint8_t *output,
                          size_t output_capacity)
{
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if (output_capacity == 0U)
    {
        return 0U;
    }

    while (read_index < input_length)
    {
        if (input[read_index] == 0U)
        {
            output[code_index] = code;
            code = 1U;
            code_index = write_index;
            ++write_index;
            if (write_index > output_capacity)
            {
                return 0U;
            }
        }
        else
        {
            if (write_index >= output_capacity)
            {
                return 0U;
            }
            output[write_index] = input[read_index];
            ++write_index;
            ++code;

            if (code == 0xFFU)
            {
                output[code_index] = code;
                code = 1U;
                code_index = write_index;
                ++write_index;
                if (write_index > output_capacity)
                {
                    return 0U;
                }
            }
        }

        ++read_index;
    }

    output[code_index] = code;
    return write_index;
}

static bool cobs_decode(const uint8_t *input, size_t input_length, uint8_t *output,
                        size_t output_capacity, size_t *output_length)
{
    size_t read_index = 0U;
    size_t write_index = 0U;

    while (read_index < input_length)
    {
        uint8_t code = input[read_index];
        if (code == 0U)
        {
            return false;
        }
        ++read_index;

        for (uint8_t offset = 1U; offset < code; ++offset)
        {
            if ((read_index >= input_length) || (write_index >= output_capacity))
            {
                return false;
            }
            output[write_index] = input[read_index];
            ++write_index;
            ++read_index;
        }

        if ((code != 0xFFU) && (read_index < input_length))
        {
            if (write_index >= output_capacity)
            {
                return false;
            }
            output[write_index] = 0U;
            ++write_index;
        }
    }

    *output_length = write_index;
    return true;
}

static const command_entry_t *find_command(const command_server_t *server, uint8_t command)
{
    for (size_t index = 0; index < server->entry_count; ++index)
    {
        if (server->entries[index].command == command)
        {
            return &server->entries[index];
        }
    }

    return NULL;
}

static void send_response(command_server_t *server, uint8_t sequence, uint8_t command,
                          command_status_t status, uint16_t payload_length)
{
    size_t raw_length = COMMAND_PROTOCOL_HEADER_SIZE + payload_length;
    uint16_t crc;
    size_t encoded_length;

    server->response_frame[FRAME_VERSION_INDEX] = COMMAND_PROTOCOL_VERSION;
    server->response_frame[FRAME_FLAGS_INDEX] = COMMAND_FRAME_FLAG_RESPONSE;
    server->response_frame[FRAME_SEQUENCE_INDEX] = sequence;
    server->response_frame[FRAME_COMMAND_INDEX] = command;
    server->response_frame[FRAME_STATUS_INDEX] = (uint8_t)status;
    write_u16_le(&server->response_frame[FRAME_LENGTH_LOW_INDEX], payload_length);

    crc = crc16_ccitt(server->response_frame, raw_length);
    write_u16_le(&server->response_frame[raw_length], crc);
    raw_length += COMMAND_PROTOCOL_CRC_SIZE;

    encoded_length = cobs_encode(server->response_frame, raw_length, server->response_encoded,
                                 sizeof(server->response_encoded) - 1U);
    if (encoded_length == 0U)
    {
        return;
    }

    server->response_encoded[encoded_length] = 0U;
    server->transport.write(server->transport.context, server->response_encoded,
                            encoded_length + 1U);
}

static bool process_frame(command_server_t *server)
{
    size_t raw_length = 0U;
    uint16_t payload_length;
    size_t expected_length;
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint8_t sequence;
    uint8_t command;
    command_status_t status = COMMAND_STATUS_OK;
    uint16_t response_length = 0U;
    const command_entry_t *entry;

    if (!cobs_decode(server->encoded_frame, server->encoded_length, server->request_frame,
                     sizeof(server->request_frame), &raw_length))
    {
        return false;
    }

    if (raw_length < (COMMAND_PROTOCOL_HEADER_SIZE + COMMAND_PROTOCOL_CRC_SIZE))
    {
        return false;
    }

    payload_length = read_u16_le(&server->request_frame[FRAME_LENGTH_LOW_INDEX]);
    expected_length = COMMAND_PROTOCOL_HEADER_SIZE + payload_length + COMMAND_PROTOCOL_CRC_SIZE;
    if ((payload_length > COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE) || (raw_length != expected_length))
    {
        return false;
    }

    received_crc = read_u16_le(&server->request_frame[raw_length - COMMAND_PROTOCOL_CRC_SIZE]);
    calculated_crc = crc16_ccitt(server->request_frame, raw_length - COMMAND_PROTOCOL_CRC_SIZE);
    if (received_crc != calculated_crc)
    {
        return false;
    }

    if ((server->request_frame[FRAME_FLAGS_INDEX] & COMMAND_FRAME_FLAG_RESPONSE) != 0U)
    {
        return false;
    }

    sequence = server->request_frame[FRAME_SEQUENCE_INDEX];
    command = server->request_frame[FRAME_COMMAND_INDEX];

    if (server->request_frame[FRAME_VERSION_INDEX] != COMMAND_PROTOCOL_VERSION)
    {
        status = COMMAND_STATUS_UNSUPPORTED_VERSION;
    }
    else if (server->request_frame[FRAME_STATUS_INDEX] != 0U)
    {
        status = COMMAND_STATUS_BAD_STATE;
    }
    else
    {
        entry = find_command(server, command);
        if (entry == NULL)
        {
            status = COMMAND_STATUS_BAD_COMMAND;
        }
        else if ((payload_length < entry->minimum_length) ||
                 (payload_length > entry->maximum_length))
        {
            status = COMMAND_STATUS_BAD_LENGTH;
        }
        else
        {
            status = entry->handler(
                entry->context, &server->request_frame[COMMAND_PROTOCOL_HEADER_SIZE],
                payload_length, &server->response_frame[COMMAND_PROTOCOL_HEADER_SIZE],
                COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE, &response_length);
            if (response_length > COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE)
            {
                status = COMMAND_STATUS_INTERNAL_ERROR;
                response_length = 0U;
            }
        }
    }

    send_response(server, sequence, command, status, response_length);
    return true;
}

void command_server_init(command_server_t *server, const command_entry_t *entries,
                         size_t entry_count, command_transport_t transport)
{
    server->entries = entries;
    server->entry_count = entry_count;
    server->transport = transport;
    server->encoded_length = 0U;
    server->discard_until_delimiter = false;
}

bool command_server_poll(command_server_t *server)
{
    uint8_t byte;
    bool handled_request = false;

    while (server->transport.read_byte(server->transport.context, &byte))
    {
        if (byte == 0U)
        {
            if (!server->discard_until_delimiter && (server->encoded_length != 0U))
            {
                handled_request |= process_frame(server);
            }

            server->encoded_length = 0U;
            server->discard_until_delimiter = false;
        }
        else if (!server->discard_until_delimiter)
        {
            if (server->encoded_length < sizeof(server->encoded_frame))
            {
                server->encoded_frame[server->encoded_length] = byte;
                ++server->encoded_length;
            }
            else
            {
                server->encoded_length = 0U;
                server->discard_until_delimiter = true;
            }
        }
    }

    return handled_request;
}
