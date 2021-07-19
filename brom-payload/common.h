#ifndef _COMMON_H_
#define _COMMON_H_

#include <inttypes.h>

extern void (*jump_bl)(void);
extern void (*send_usb_response)(int, int, int);
extern void (**ptr_send)();
extern void (**ptr_recv)();
extern void (*orig_ptr_send)();
extern void (*orig_ptr_recv)();

extern int (*send_dword)();
extern int (*recv_dword)();
// addr, sz
extern int (*send_data)();
// addr, sz, flags (=0)
extern int (*recv_data)();

extern void low_uart_put(int ch);

extern void _putchar(char character);

#endif
