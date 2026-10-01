#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMMAND_PROTOCOL_VERSION 1U
#define COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE 128U

#define COMMAND_FRAME_FLAG_RESPONSE 0x01U

typedef enum
{
    COMMAND_STATUS_OK = 0,
    COMMAND_STATUS_BAD_COMMAND = 1,
    COMMAND_STATUS_BAD_LENGTH = 2,
    COMMAND_STATUS_BAD_STATE = 3,
    COMMAND_STATUS_UNSUPPORTED_VERSION = 4,
    COMMAND_STATUS_INTERNAL_ERROR = 5,
    COMMAND_STATUS_BAD_ARGUMENT = 6,
    COMMAND_STATUS_FLASH_ERROR = 7,
    COMMAND_STATUS_VERIFY_ERROR = 8,
} command_status_t;

typedef command_status_t (*command_handler_t)(const void *context, const uint8_t *request,
                                              uint16_t request_length, uint8_t *response,
                                              uint16_t response_capacity,
                                              uint16_t *response_length);

typedef struct
{
    uint8_t command;
    uint16_t minimum_length;
    uint16_t maximum_length;
    command_handler_t handler;
    const void *context;
} command_entry_t;

typedef bool (*command_read_byte_t)(void *context, uint8_t *byte);
typedef void (*command_write_t)(void *context, const uint8_t *data, size_t length);

typedef struct
{
    command_read_byte_t read_byte;
    command_write_t write;
    void *context;
} command_transport_t;

#define COMMAND_PROTOCOL_HEADER_SIZE 7U
#define COMMAND_PROTOCOL_CRC_SIZE 2U
#define COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE                                                        \
    (COMMAND_PROTOCOL_HEADER_SIZE + COMMAND_PROTOCOL_MAX_PAYLOAD_SIZE + COMMAND_PROTOCOL_CRC_SIZE)
#define COMMAND_PROTOCOL_MAX_ENCODED_FRAME_SIZE (COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE + 2U)

typedef struct
{
    const command_entry_t *entries;
    size_t entry_count;
    command_transport_t transport;
    uint8_t encoded_frame[COMMAND_PROTOCOL_MAX_ENCODED_FRAME_SIZE];
    uint8_t request_frame[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    uint8_t response_frame[COMMAND_PROTOCOL_MAX_RAW_FRAME_SIZE];
    uint8_t response_encoded[COMMAND_PROTOCOL_MAX_ENCODED_FRAME_SIZE + 1U];
    uint16_t encoded_length;
    bool discard_until_delimiter;
} command_server_t;

void command_server_init(command_server_t *server, const command_entry_t *entries,
                         size_t entry_count, command_transport_t transport);

/* Returns true after at least one CRC-valid request frame has been handled. */
bool command_server_poll(command_server_t *server);

#endif
