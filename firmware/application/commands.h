#ifndef APPLICATION_COMMANDS_H
#define APPLICATION_COMMANDS_H

#include <stdbool.h>

#include "command_protocol.h"

void application_commands_init(command_server_t *server);
bool application_commands_take_bootloader_request(void);

#endif
