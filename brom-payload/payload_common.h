#ifndef _PAYLOAD_COMMON_H_
#define _PAYLOAD_COMMON_H_

#include "printf.h"

#include "libc.h"

#include "drivers/types.h"
#include "drivers/core.h"
#include "drivers/mt_sd.h"
#include "drivers/errno.h"
#include "drivers/mmc.h"


void sleepy(void);
void mdelay (unsigned long msec);
/* delay usec useconds */
void udelay (unsigned long usec);
void hex_dump(const void* data, size_t size);
void command_loop();

#endif
