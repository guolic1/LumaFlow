#ifndef QMA6100P_H
#define QMA6100P_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    QMA6100P_OK = 0,
    QMA6100P_BUS_ERROR = 1,
    QMA6100P_CHIP_ID_ERROR = 2,
    QMA6100P_CONFIG_ERROR = 3,
} qma6100p_status_t;

typedef struct
{
    bool (*read)(uint8_t reg, uint8_t *data, uint8_t length);
    bool (*write)(uint8_t reg, uint8_t value);
    void (*delay_ms)(uint32_t duration);
} qma6100p_bus_t;

typedef struct
{
    qma6100p_bus_t bus;
    bool ready;
} qma6100p_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
    uint8_t new_data;
} qma6100p_sample_t;

/* Fixed configuration: address is selected by the bus, +/-2g, nominal 100Hz. */
qma6100p_status_t qma6100p_init(qma6100p_t *device, qma6100p_bus_t bus);
qma6100p_status_t qma6100p_read(qma6100p_t *device, qma6100p_sample_t *sample);

#endif
