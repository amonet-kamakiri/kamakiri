#include "common.h"

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
