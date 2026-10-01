#ifndef BOOTLOADER_COMMANDS_H
#define BOOTLOADER_COMMANDS_H

#include <stdbool.h>

#include "command_protocol.h"

void bootloader_commands_init(command_server_t *server);
bool bootloader_commands_take_jump_request(void);

#endif
