#ifndef JUMP_TO_APPLICATION_H
#define JUMP_TO_APPLICATION_H

#include <stdbool.h>

bool bootloader_application_is_valid(void);
void bootloader_jump_to_application(void);

#endif
