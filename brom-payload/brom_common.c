#include "brom_common.h"

void (*send_usb_response)(int, int, int) = (void*)0x55bb;

int (*send_dword)() = (void*)0xBE09;
int (*recv_dword)() = (void*)0xBDD5;
// addr, sz
int (*send_data)() = (void*)0xBED1;
// addr, sz, flags (=0)
int (*recv_data)() = (void*)0xBE4B;

