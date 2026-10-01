#include "application_update.h"

#include <stdbool.h>
#include <stdint.h>

#include "application_image.h"
#include "main.h"

#define FLASH_KEY1_VALUE 0x45670123UL
#define FLASH_KEY2_VALUE 0xCDEF89ABUL
#define FLASH_PAGE_SIZE 0x800UL
#define FLASH_OPERATION_TIMEOUT 10000000UL

#define APPLICATION_FIRST_PAGE ((APPLICATION_FLASH_START - FLASH_BASE) / FLASH_PAGE_SIZE)
#define APPLICATION_LAST_PAGE ((APPLICATION_FLASH_END - FLASH_BASE) / FLASH_PAGE_SIZE - 1UL)

#define FLASH_OPERATION_ERROR_MASK                                                                 \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |     \
     FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR)
#define FLASH_CLEAR_MASK (FLASH_OPERATION_ERROR_MASK | FLASH_SR_OPTVERR | FLASH_SR_EOP)

typedef struct
{
    uint32_t image_size;
    uint32_t expected_crc32;
    uint32_t firmware_version;
    uint32_t next_offset;
    uint8_t vector[8];
    application_update_state_t state;
    application_update_result_t last_result;
} application_update_context_t;

static application_update_context_t update_context;

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t *data)
{
    return (uint64_t)read_u32_le(data) | ((uint64_t)read_u32_le(&data[4]) << 32U);
}

static bool flash_wait_for_operation(void)
{
    uint32_t timeout = FLASH_OPERATION_TIMEOUT;

    while (((FLASH->SR & FLASH_SR_BSY1) != 0U) && (timeout != 0U))
    {
        --timeout;
    }
    if (timeout == 0U)
    {
        return false;
    }

    if ((FLASH->SR & FLASH_OPERATION_ERROR_MASK) != 0U)
    {
        FLASH->SR = FLASH_CLEAR_MASK;
        return false;
    }

    FLASH->SR = FLASH_CLEAR_MASK;
    timeout = FLASH_OPERATION_TIMEOUT;
    while (((FLASH->SR & FLASH_SR_CFGBSY) != 0U) && (timeout != 0U))
    {
        --timeout;
    }

    return timeout != 0U;
}

static bool flash_unlock(void)
{
    if (!flash_wait_for_operation())
    {
        return false;
    }

    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = FLASH_KEY1_VALUE;
        FLASH->KEYR = FLASH_KEY2_VALUE;
    }

    return (FLASH->CR & FLASH_CR_LOCK) == 0U;
}

static void flash_lock(void)
{
    (void)flash_wait_for_operation();
    FLASH->CR |= FLASH_CR_LOCK;
}

static bool flash_erase_page(uint32_t page)
{
    uint32_t control = FLASH->CR & ~(FLASH_CR_PNB | FLASH_CR_PER);

    FLASH->CR = control | FLASH_CR_PER | FLASH_CR_STRT | (page << FLASH_CR_PNB_Pos);
    if (!flash_wait_for_operation())
    {
        FLASH->CR &= ~FLASH_CR_PER;
        return false;
    }

    FLASH->CR &= ~FLASH_CR_PER;
    return true;
}

static bool flash_program_double_word(uint32_t address, uint64_t value)
{
    uint32_t primask;

    if (((address & 0x7U) != 0U) || !flash_wait_for_operation())
    {
        return false;
    }

    FLASH->CR |= FLASH_CR_PG;
    primask = __get_PRIMASK();
    __disable_irq();
    *(volatile uint32_t *)(uintptr_t)address = (uint32_t)value;
    __ISB();
    *(volatile uint32_t *)(uintptr_t)(address + 4U) = (uint32_t)(value >> 32U);
    __set_PRIMASK(primask);

    if (!flash_wait_for_operation())
    {
        FLASH->CR &= ~FLASH_CR_PG;
        return false;
    }
    FLASH->CR &= ~FLASH_CR_PG;

    return (*(const volatile uint32_t *)(uintptr_t)address == (uint32_t)value) &&
           (*(const volatile uint32_t *)(uintptr_t)(address + 4U) == (uint32_t)(value >> 32U));
}

static bool erase_application(void)
{
    if (!flash_unlock())
    {
        return false;
    }

    /* Erase the commit marker first so a reset can never boot a partly erased image. */
    if (!flash_erase_page(APPLICATION_LAST_PAGE))
    {
        flash_lock();
        return false;
    }

    for (uint32_t page = APPLICATION_FIRST_PAGE; page < APPLICATION_LAST_PAGE; ++page)
    {
        if (!flash_erase_page(page))
        {
            flash_lock();
            return false;
        }
    }

    flash_lock();
    return true;
}

static application_update_result_t fail_update(application_update_result_t result)
{
    update_context.state = APPLICATION_UPDATE_STATE_FAILED;
    update_context.last_result = result;
    return result;
}

void application_update_init(void)
{
    update_context.image_size = 0U;
    update_context.expected_crc32 = 0U;
    update_context.firmware_version = 0U;
    update_context.next_offset = 0U;
    update_context.state = APPLICATION_UPDATE_STATE_IDLE;
    update_context.last_result = APPLICATION_UPDATE_RESULT_OK;
}

application_update_result_t application_update_begin(uint32_t image_size, uint32_t image_crc32,
                                                     uint32_t firmware_version)
{
    if (update_context.state == APPLICATION_UPDATE_STATE_RECEIVING)
    {
        update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_STATE;
        return update_context.last_result;
    }
    if ((image_size < 8U) || (image_size > APPLICATION_MAX_IMAGE_SIZE))
    {
        update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_ARGUMENT;
        return update_context.last_result;
    }

    update_context.image_size = image_size;
    update_context.expected_crc32 = image_crc32;
    update_context.firmware_version = firmware_version;
    update_context.next_offset = 0U;
    update_context.state = APPLICATION_UPDATE_STATE_FAILED;

    if (!erase_application())
    {
        return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
    }

    update_context.state = APPLICATION_UPDATE_STATE_RECEIVING;
    update_context.last_result = APPLICATION_UPDATE_RESULT_OK;
    return APPLICATION_UPDATE_RESULT_OK;
}

application_update_result_t application_update_write(uint32_t offset, const uint8_t *data,
                                                     size_t length)
{
    uint32_t write_offset;
    size_t data_offset;

    if (update_context.state != APPLICATION_UPDATE_STATE_RECEIVING)
    {
        update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_STATE;
        return update_context.last_result;
    }
    if ((data == NULL) || (length == 0U) || (length > APPLICATION_UPDATE_MAX_CHUNK_SIZE) ||
        (offset != update_context.next_offset) || ((offset & 0x7U) != 0U) ||
        (offset > update_context.image_size) ||
        (length > (size_t)(update_context.image_size - offset)) ||
        (((length & 0x7U) != 0U) && ((offset + length) != update_context.image_size)))
    {
        update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_ARGUMENT;
        return update_context.last_result;
    }

    data_offset = 0U;
    write_offset = offset;
    if (offset == 0U)
    {
        uint32_t stack_pointer;
        uint32_t reset_handler;

        if (length < sizeof(update_context.vector))
        {
            update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_ARGUMENT;
            return update_context.last_result;
        }

        stack_pointer = read_u32_le(data);
        reset_handler = read_u32_le(&data[4]);
        if (!application_vector_is_valid(stack_pointer, reset_handler, update_context.image_size))
        {
            update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_ARGUMENT;
            return update_context.last_result;
        }

        for (size_t index = 0U; index < sizeof(update_context.vector); ++index)
        {
            update_context.vector[index] = data[index];
        }
        data_offset = sizeof(update_context.vector);
        write_offset = sizeof(update_context.vector);
    }

    if (data_offset < length)
    {
        if (!flash_unlock())
        {
            return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
        }

        while (data_offset < length)
        {
            uint8_t double_word[8] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
            size_t copy_length = length - data_offset;

            if (copy_length > sizeof(double_word))
            {
                copy_length = sizeof(double_word);
            }
            for (size_t index = 0U; index < copy_length; ++index)
            {
                double_word[index] = data[data_offset + index];
            }

            if (!flash_program_double_word(APPLICATION_FLASH_START + write_offset,
                                           read_u64_le(double_word)))
            {
                flash_lock();
                return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
            }
            data_offset += copy_length;
            write_offset += sizeof(double_word);
        }

        flash_lock();
    }

    update_context.next_offset += (uint32_t)length;
    update_context.last_result = APPLICATION_UPDATE_RESULT_OK;
    return APPLICATION_UPDATE_RESULT_OK;
}

application_update_result_t application_update_end(void)
{
    uint32_t crc;
    uint64_t metadata_words[3];

    if ((update_context.state != APPLICATION_UPDATE_STATE_RECEIVING) ||
        (update_context.next_offset != update_context.image_size))
    {
        update_context.last_result = APPLICATION_UPDATE_RESULT_BAD_STATE;
        return update_context.last_result;
    }

    crc = application_crc32_update(APPLICATION_CRC32_INITIAL, update_context.vector,
                                   sizeof(update_context.vector));
    crc = application_crc32_update(
        crc, (const uint8_t *)(uintptr_t)(APPLICATION_FLASH_START + sizeof(update_context.vector)),
        update_context.image_size - sizeof(update_context.vector));
    if (application_crc32_finish(crc) != update_context.expected_crc32)
    {
        return fail_update(APPLICATION_UPDATE_RESULT_VERIFY_ERROR);
    }

    metadata_words[0] = (uint64_t)APPLICATION_METADATA_MAGIC |
                        ((uint64_t)APPLICATION_METADATA_FORMAT_VERSION << 32U) |
                        ((uint64_t)sizeof(application_metadata_t) << 48U);
    metadata_words[1] =
        (uint64_t)update_context.image_size | ((uint64_t)update_context.expected_crc32 << 32U);
    metadata_words[2] = (uint64_t)update_context.firmware_version;

    if (!flash_unlock())
    {
        return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
    }

    for (size_t index = 0U; index < 3U; ++index)
    {
        if (!flash_program_double_word(APPLICATION_METADATA_ADDRESS + (uint32_t)(index * 8U),
                                       metadata_words[index]))
        {
            flash_lock();
            return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
        }
    }

    if (!flash_program_double_word(APPLICATION_FLASH_START, read_u64_le(update_context.vector)))
    {
        flash_lock();
        return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
    }

    if (application_crc32((const uint8_t *)(uintptr_t)APPLICATION_FLASH_START,
                          update_context.image_size) != update_context.expected_crc32)
    {
        flash_lock();
        return fail_update(APPLICATION_UPDATE_RESULT_VERIFY_ERROR);
    }

    if (!flash_program_double_word(APPLICATION_METADATA_ADDRESS + 24U,
                                   APPLICATION_METADATA_COMMIT_MARKER))
    {
        flash_lock();
        return fail_update(APPLICATION_UPDATE_RESULT_FLASH_ERROR);
    }

    flash_lock();
    if (!application_image_is_valid())
    {
        return fail_update(APPLICATION_UPDATE_RESULT_VERIFY_ERROR);
    }

    update_context.state = APPLICATION_UPDATE_STATE_COMPLETE;
    update_context.last_result = APPLICATION_UPDATE_RESULT_OK;
    return APPLICATION_UPDATE_RESULT_OK;
}

void application_update_get_status(application_update_status_t *status)
{
    status->state = update_context.state;
    status->last_result = update_context.last_result;
    status->next_offset = update_context.next_offset;
    status->image_size = update_context.image_size;
}
