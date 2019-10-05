#include "common.h"

void (*jump_bl)(void) = (void*) 0xB673;
void (*send_usb_response)(int, int, int) = (void*)0x55bb;
void (**ptr_send)() = (void*)0x103088;
void (**ptr_recv)() = (void*)0x103084;
void (*orig_ptr_send)();
void (*orig_ptr_recv)();

int (*send_dword)() = (void*)0xBE09;
int (*recv_dword)() = (void*)0xBDD5;
// addr, sz
int (*send_data)() = (void*)0xBED1;
// addr, sz, flags (=0)
int (*recv_data)() = (void*)0xBE4B;

void low_uart_put(int ch) {
    volatile uint32_t *uart_reg0 = (volatile uint32_t*)0x11003014;
    volatile uint32_t *uart_reg1 = (volatile uint32_t*)0x11003000;

    while ( !((*uart_reg0) & 0x20) )
    {}

    *uart_reg1 = ch;
}

void _putchar(char character)
{
    if (character == '\n')
        low_uart_put('\r');
    low_uart_put(character);
}
