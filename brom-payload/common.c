#include "common.h"

void (*send_usb_response)(int, int, int) = (void*)0x2D2B;

int (*send_dword)() = (void*)0xBCD3;
int (*recv_dword)() = (void*)0xBC9F;
// addr, sz
int (*send_data)() = (void*)0xBDA3;
// addr, sz, flags (=0)
int (*recv_data)() = (void*)0xBD15;

void low_uart_put(int ch) {
    volatile uint32_t *uart_reg0 = (volatile uint32_t*)0x11002014;
    volatile uint32_t *uart_reg1 = (volatile uint32_t*)0x11002000;

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
