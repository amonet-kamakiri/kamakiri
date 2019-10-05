#ifndef _COMMON_H_
#define _COMMON_H_

#include <inttypes.h>

void (*jump_bl)(void);
void (*send_usb_response)(int, int, int);
void (**ptr_send)();
void (**ptr_recv)();
void (*orig_ptr_send)();
void (*orig_ptr_recv)();

int (*send_dword)();
int (*recv_dword)();
// addr, sz
int (*send_data)();
// addr, sz, flags (=0)
int (*recv_data)();

void low_uart_put(int ch);

void _putchar(char character);

#endif
