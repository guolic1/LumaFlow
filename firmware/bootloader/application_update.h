#ifndef APPLICATION_UPDATE_H
#define APPLICATION_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#define APPLICATION_UPDATE_MAX_CHUNK_SIZE 120U

typedef enum
{
    APPLICATION_UPDATE_STATE_IDLE = 0,
    APPLICATION_UPDATE_STATE_RECEIVING = 1,
    APPLICATION_UPDATE_STATE_COMPLETE = 2,
    APPLICATION_UPDATE_STATE_FAILED = 3,
} application_update_state_t;

typedef enum
{
    APPLICATION_UPDATE_RESULT_OK = 0,
    APPLICATION_UPDATE_RESULT_BAD_STATE = 1,
    APPLICATION_UPDATE_RESULT_BAD_ARGUMENT = 2,
    APPLICATION_UPDATE_RESULT_FLASH_ERROR = 3,
    APPLICATION_UPDATE_RESULT_VERIFY_ERROR = 4,
} application_update_result_t;

typedef struct
{
    application_update_state_t state;
    application_update_result_t last_result;
    uint32_t next_offset;
    uint32_t image_size;
} application_update_status_t;

void application_update_init(void);
application_update_result_t application_update_begin(uint32_t image_size, uint32_t image_crc32,
                                                     uint32_t firmware_version);
application_update_result_t application_update_write(uint32_t offset, const uint8_t *data,
                                                     size_t length);
application_update_result_t application_update_end(void);
void application_update_get_status(application_update_status_t *status);

#endif
